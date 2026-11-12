// SPDX-License-Identifier: GPL-3.0-or-later

//! Bracket fixer invariants, through the string-level functions the FFI
//! exports (`fix_brackets`, `check_brackets`).

use super::bracket_gen::{gen_code, ref_balanced, strip_brackets, Flavor};
use super::{cases, minimize, random_soup, run_parallel, Rng};
use crate::bracket_fix::{check_brackets, fix_brackets, fix_brackets_full, fix_brackets_report};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::{Duration, Instant};

fn no_panic<T>(f: impl FnOnce() -> T) -> Result<T, String> {
    catch_unwind(AssertUnwindSafe(f)).map_err(|e| {
        e.downcast_ref::<String>()
            .cloned()
            .or_else(|| e.downcast_ref::<&str>().map(|s| s.to_string()))
            .unwrap_or_else(|| "panic".into())
    })
}

fn flavor(rng: &mut Rng) -> Flavor {
    if rng.chance(2, 3) {
        Flavor::CLike
    } else {
        Flavor::Hash
    }
}

/// The checker reports no BRACKET issue (shell keyword notes aside).
fn brackets_ok(s: &str) -> bool {
    !check_brackets(s).lines().any(|l| l.starts_with("line "))
}

/// Invariants that hold for ANY input.
pub fn check_any(x: &str) -> Option<(String, String)> {
    let full = fix_brackets_full(x);
    let out = full.text;
    let changed = out != x;
    if changed && !full.report.contains("line ") {
        return Some(("fix: report does not say where".into(), full.report));
    }
    if changed && !brackets_ok(&out) {
        return Some((
            "fix: result fails its own check".into(),
            check_brackets(&out),
        ));
    }
    if strip_brackets(&out) != strip_brackets(x) {
        return Some(("fix: changed/reordered a NON-bracket character".into(), out));
    }
    let again = fix_brackets(&out);
    if again != out {
        return Some(("fix: not idempotent".into(), again));
    }
    None
}

#[test]
fn fuzz_brackets_balanced_unchanged() {
    let started = Instant::now();
    let n = cases(40_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let fl = flavor(&mut rng);
        let size = 2 + rng.below(40);
        let code = gen_code(&mut rng, fl, size);
        let x = &code.text;
        if !ref_balanced(x, fl) {
            return tally.fail("generator produced unbalanced text", seed, x, String::new());
        }
        match no_panic(|| fix_brackets(x)) {
            Err(p) => tally.fail("PANIC", seed, x, p),
            Ok(out) if out != *x => {
                let min = minimize(x, |s| ref_balanced(s, fl) && fix_brackets(s) != s);
                let shown = fix_brackets(&min);
                tally.fail(
                    &format!("fix: TOUCHED balanced input ({fl:?})"),
                    seed,
                    &min,
                    format!("out={shown:?}"),
                )
            }
            _ => {}
        }
        let rep = check_brackets(x);
        if !rep.contains("No issues") {
            let min = minimize(x, |s| {
                ref_balanced(s, fl) && !check_brackets(s).contains("No issues")
            });
            tally.fail(
                &format!("check: FALSE issue on balanced input ({fl:?})"),
                seed,
                &min,
                check_brackets(&min),
            );
        }
    });
    let r = t.report("brackets: balanced input", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

/// One bracket deleted or inserted at a random code position.
#[test]
fn fuzz_brackets_single_mutation() {
    let started = Instant::now();
    let n = cases(40_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let fl = flavor(&mut rng);
        let size = 2 + rng.below(40);
        let code = gen_code(&mut rng, fl, size);
        let x = &code.text;
        let (y, kind) = if rng.chance(1, 2) && !code.brackets.is_empty() {
            let at = *rng.pick(&code.brackets);
            let c = x.as_bytes()[at] as char;
            let kind = if matches!(c, '(' | '[' | '{') {
                "delete-opener"
            } else {
                "delete-closer"
            };
            (format!("{}{}", &x[..at], &x[at + 1..]), kind)
        } else {
            let at = *rng.pick(&code.gaps);
            let c = *rng.pick(&['(', ')', '[', ']', '{', '}']);
            let kind = if matches!(c, '(' | '[' | '{') {
                "insert-opener"
            } else {
                "insert-closer"
            };
            (format!("{}{}{}", &x[..at], c, &x[at..]), kind)
        };
        let out = match no_panic(|| fix_brackets(&y)) {
            Ok(o) => o,
            Err(p) => return tally.fail("PANIC", seed, &y, p),
        };
        if let Some((class, detail)) = check_any(&y) {
            return tally.fail(&class, seed, &y, detail);
        }
        let outcome = if out == *x {
            "exact-restore"
        } else if out == y {
            if ref_balanced(&y, fl) {
                "input-still-balanced"
            } else {
                "declined"
            }
        } else if !brackets_ok(&out) {
            // hard invariant: what the fixer returns must re-check clean
            tally.fail(
                &format!("fix: result fails its own check ({fl:?} {kind})"),
                seed,
                &y,
                format!("orig={x:?}\n    out={out:?}\n    {}", check_brackets(&out)),
            );
            "SELF-UNBALANCED"
        } else if ref_balanced(&out, fl) {
            "balanced-differs"
        } else {
            // Balanced under the reading the fixer chose and reported, but
            // not under the generator's language: the text carries no
            // language, and a mutation inside a JS template hole or next to
            // a quote can make another reading need fewer edits. Measured,
            // with a ceiling below.
            tally.info(
                &format!("fix: other language reading than the generator's ({fl:?} {kind})"),
                seed,
                &y,
                format!(
                    "orig={x:?}\n    out={out:?}\n    {}",
                    fix_brackets_report(&y)
                ),
            );
            "OTHER-READING"
        };
        tally.note(&format!("{fl:?} {kind} => {outcome}"));
    });
    let r = t.report("brackets: one mutation", started);
    println!("{r}");
    println!("{}", mutation_table(&t));
    assert!(t.examples.is_empty(), "{r}");
    let other: u64 = t
        .counts
        .iter()
        .filter(|(k, _)| k.ends_with("=> OTHER-READING"))
        .map(|(_, v)| v)
        .sum();
    assert!(
        other * 100 <= t.cases,
        "more than 1% of mutations resolved under a different reading: {other}/{}",
        t.cases
    );
}

/// Outcome rates per flavor and mutation kind.
pub fn mutation_table(t: &super::Tally) -> String {
    use std::collections::BTreeMap;
    let cols = [
        "exact-restore",
        "balanced-differs",
        "OTHER-READING",
        "declined",
        "input-still-balanced",
        "SELF-UNBALANCED",
    ];
    let mut rows: BTreeMap<String, [u64; 6]> = BTreeMap::new();
    for (k, v) in &t.counts {
        if let Some((row, outcome)) = k.split_once(" => ") {
            if let Some(i) = cols.iter().position(|c| *c == outcome) {
                rows.entry(row.to_string()).or_insert([0; 6])[i] += v;
            }
        }
    }
    let mut s = format!("{:<24}", "flavor/mutation");
    for c in cols {
        s.push_str(&format!(" {c:>20}"));
    }
    s.push('\n');
    for (k, r) in rows {
        let total: u64 = r.iter().sum();
        s.push_str(&format!("{k:<24}"));
        for v in r {
            s.push_str(&format!(
                " {:>11} ({:>5.1}%)",
                v,
                100.0 * v as f64 / total.max(1) as f64
            ));
        }
        s.push('\n');
    }
    s
}

#[test]
fn fuzz_brackets_soup_never_panics() {
    let started = Instant::now();
    let n = cases(60_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let y = random_soup(&mut rng, 60);
        match no_panic(|| {
            let _ = check_brackets(&y);
            check_any(&y)
        }) {
            Err(p) => {
                let min = minimize(&y, |s| {
                    no_panic(|| {
                        let _ = check_brackets(s);
                        check_any(s)
                    })
                    .is_err()
                });
                tally.fail("PANIC", seed, &min, p)
            }
            Ok(Some((class, detail))) => {
                let min = minimize(
                    &y,
                    |s| matches!(no_panic(|| check_any(s)), Ok(Some((c, _))) if c == class),
                );
                tally.fail(&class, seed, &min, detail)
            }
            Ok(None) => {}
        }
    });
    let r = t.report("brackets: soup", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

fn timed(label: &str, budget: Duration, f: impl FnOnce()) {
    let t0 = Instant::now();
    f();
    let el = t0.elapsed();
    let budget = if cfg!(debug_assertions) {
        budget * 15
    } else {
        budget
    };
    assert!(el < budget, "{label}: took {el:?} (budget {budget:?})");
    println!("{label}: {el:?}");
}

#[test]
fn brackets_pathological_shapes() {
    let opens = "(".repeat(2_000_000);
    timed("2M '('", Duration::from_secs(15), || {
        let out = fix_brackets(&opens);
        assert!(strip_brackets(&out).is_empty(), "non-bracket change");
        let _ = check_brackets(&opens);
    });
    let closes = "]".repeat(2_000_000);
    timed("2M ']'", Duration::from_secs(15), || {
        let _ = fix_brackets(&closes);
        let _ = check_brackets(&closes);
    });
    let mixed = "( ]".repeat(500_000);
    timed("500k '( ]'", Duration::from_secs(15), || {
        let _ = fix_brackets(&mixed);
        let _ = check_brackets(&mixed);
    });
    let deep = format!("{}{}", "{[(".repeat(300_000), ")]}".repeat(300_000));
    timed("900k-deep balanced", Duration::from_secs(15), || {
        assert!(fix_brackets(&deep) == deep, "touched balanced");
    });
    let quotes = "'\"`".repeat(1_000_000);
    timed("3M quotes", Duration::from_secs(15), || {
        let _ = fix_brackets(&quotes);
        let _ = check_brackets(&quotes);
    });
    let mut code = String::new();
    while code.len() < 6_000_000 {
        code.push_str("fn f(a: [u8; 4]) { let s = \"(\"; // )\n    g(a[0], { x: 1 });\n}\n");
    }
    let broken = format!("{}{}", &code[..3_000_000], &code[3_000_001..]);
    timed(
        "6 MB code, one bracket off",
        Duration::from_secs(15),
        || {
            assert!(fix_brackets(&code) == code, "touched balanced");
            let out = fix_brackets(&broken);
            assert!(
                strip_brackets(&out) == strip_brackets(&broken),
                "non-bracket change"
            );
            let _ = check_brackets(&broken);
        },
    );
    let comments = "/*(".repeat(1_000_000);
    timed("1M unterminated '/*('", Duration::from_secs(15), || {
        let _ = fix_brackets(&comments);
        let _ = check_brackets(&comments);
    });
    let slashes = format!("x = {}", "(/".repeat(1_000_000));
    timed("1M '(/' on one line", Duration::from_secs(15), || {
        let _ = fix_brackets(&slashes);
    });
    let lua = "--[[(".repeat(1_000_000);
    timed("1M unterminated '--[[('", Duration::from_secs(15), || {
        let _ = fix_brackets(&lua);
    });
    let heredocs = "cat <<EOF(\n".repeat(200_000);
    timed(
        "200k unterminated heredocs",
        Duration::from_secs(15),
        || {
            let _ = fix_brackets(&heredocs);
        },
    );
    let templates = "`${(".repeat(500_000);
    timed(
        "500k unterminated template holes",
        Duration::from_secs(15),
        || {
            let _ = fix_brackets(&templates);
        },
    );
    let lines = "x(\n".repeat(1_000_000);
    timed(
        "1M lines each with an unclosed '('",
        Duration::from_secs(15),
        || {
            let _ = fix_brackets(&lines);
        },
    );
}
