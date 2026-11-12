// SPDX-License-Identifier: GPL-3.0-or-later

//! JSON invariants, exercised through the same string-level functions the
//! FFI exports (`format_json`, `minify_json`, `json_parse_error`,
//! `fix_json_with_report`).

use super::json_gen::{self, boundaries, corrupt, gen_doc, Corruption, GenOpts};
use super::json_ref::{ref_parse, ref_tokens};
use super::{cases, minimize, random_soup, run_parallel, Rng, Tally};
use crate::json_fmt::{fix_json_with_report, format_json, json_parse_error, minify_json};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::time::{Duration, Instant};

fn opts(rng: &mut Rng) -> GenOpts {
    GenOpts {
        max_depth: 1 + rng.below(6),
        max_items: 1 + rng.below(5),
        lone_surrogates: true,
    }
}

/// Offset → (line, column) using the editor's convention: `\n`, `\r\n` and a
/// lone `\r` each end a line; columns count chars, 1-based.
pub fn line_col(s: &str, off: usize) -> (usize, usize) {
    let mut line = 1;
    let mut col = 1;
    let mut it = s[..off].chars().peekable();
    while let Some(c) = it.next() {
        if c == '\n' || (c == '\r' && it.peek() != Some(&'\n')) {
            line += 1;
            col = 1;
        } else if c == '\r' {
            // the '\r' of a CRLF: the '\n' ends the line
        } else {
            col += 1;
        }
    }
    (line, col)
}

/// Pull "line L column C" out of an error message.
pub fn reported_pos(msg: &str) -> Option<(usize, usize)> {
    let l = msg.find("line ")?;
    let rest = &msg[l + 5..];
    let ln: usize = rest
        .split(|c: char| !c.is_ascii_digit())
        .next()?
        .parse()
        .ok()?;
    let c = rest.find("column ")?;
    let col: usize = rest[c + 7..]
        .split(|c: char| !c.is_ascii_digit())
        .next()?
        .parse()
        .ok()?;
    Some((ln, col))
}

fn no_panic<T>(f: impl FnOnce() -> T) -> Result<T, String> {
    catch_unwind(AssertUnwindSafe(f)).map_err(|e| {
        e.downcast_ref::<String>()
            .cloned()
            .or_else(|| e.downcast_ref::<&str>().map(|s| s.to_string()))
            .unwrap_or_else(|| "panic".into())
    })
}

/// Format / minify invariants on one VALID document. Returns the failure
/// class or None.
pub fn check_valid_doc(x: &str, indent: usize) -> Option<(String, String)> {
    let toks = ref_tokens(x)?; // generator bug guard: x must be valid
    let val = ref_parse(x);
    if !json_parse_error(x).is_empty() {
        return Some(("validate: rejected valid JSON".into(), json_parse_error(x)));
    }
    let out = format_json(x, indent);
    match ref_tokens(&out) {
        None => return Some(("format: output is not valid JSON".into(), out)),
        Some(t) if t != toks => {
            let v2 = ref_parse(&out);
            let class = if v2 != val {
                "format: MEANING changed (keys/numbers/strings)"
            } else {
                "format: token text changed (escapes re-encoded)"
            };
            return Some((class.into(), out));
        }
        _ => {}
    }
    if x.starts_with('\u{FEFF}') != out.starts_with('\u{FEFF}') {
        return Some(("format: BOM added/dropped".into(), out));
    }
    if out.lines().any(|l| l.ends_with(' ') || l.ends_with('\t')) {
        return Some(("format: trailing whitespace on a line".into(), out));
    }
    let again = format_json(&out, indent);
    if again != out {
        return Some(("format: not idempotent".into(), again));
    }
    let m = minify_json(x);
    match ref_tokens(&m) {
        Some(t) if t == toks => {}
        Some(_) => return Some(("minify: token text changed".into(), m)),
        None => return Some(("minify: output is not valid JSON".into(), m)),
    }
    if minify_json(&out) != m {
        return Some(("minify(format(x)) != minify(x)".into(), m));
    }
    let (fixed, _) = fix_json_with_report(x);
    if fixed != x {
        return Some(("repair: touched VALID JSON".into(), fixed));
    }
    None
}

/// Invariants on one INVALID input.
pub fn check_invalid(y: &str) -> Option<(String, String)> {
    let err = json_parse_error(y);
    if err.is_empty() {
        return Some(("validate: accepted invalid JSON".into(), String::new()));
    }
    if reported_pos(&err).is_none() {
        return Some(("validate: error has no line/column".into(), err));
    }
    let f = format_json(y, 4);
    if f != y {
        return Some((
            "format: returned a MODIFIED document for invalid input".into(),
            f,
        ));
    }
    let m = minify_json(y);
    if m != y {
        return Some((
            "minify: returned a MODIFIED document for invalid input".into(),
            m,
        ));
    }
    None
}

#[test]
fn fuzz_json_format_minify_valid() {
    let started = Instant::now();
    let n = cases(40_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let o = opts(&mut rng);
        let d = gen_doc(&mut rng, &o);
        let mut x = d.render();
        if rng.chance(1, 20) {
            x.insert(0, '\u{FEFF}');
        }
        let indent = *rng.pick(&[0usize, 1, 2, 4, 8]);
        match no_panic(|| check_valid_doc(&x, indent)) {
            Err(p) => tally.fail("PANIC", seed, &x, p),
            Ok(Some((class, detail))) => {
                let min = minimize(
                    &x,
                    |s| matches!(no_panic(|| check_valid_doc(s, indent)), Ok(Some((c, _))) if c == class),
                );
                tally.fail(
                    &class,
                    seed,
                    &min,
                    format!("indent={indent} out={detail:?}"),
                )
            }
            Ok(None) => {}
        }
    });
    let r = t.report("json format/minify on valid docs", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

#[test]
fn fuzz_json_invalid_inputs_untouched() {
    let started = Instant::now();
    let n = cases(40_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let y = if rng.chance(1, 3) {
            random_soup(&mut rng, 40)
        } else {
            let o = opts(&mut rng);
            let d = gen_doc(&mut rng, &o);
            let c = *rng.pick(json_gen::ALL);
            match corrupt(&mut rng, &d, c) {
                Some(d2) => d2.render(),
                None => return,
            }
        };
        if ref_parse(&y).is_some() {
            // Still valid after the corruption: the valid-doc invariants
            // apply instead (and must hold for odd shapes too).
            if let Ok(Some((class, detail))) = no_panic(|| check_valid_doc(&y, 2)) {
                tally.fail(&class, seed, &y, detail);
            }
            return;
        }
        match no_panic(|| check_invalid(&y)) {
            Err(p) => tally.fail("PANIC", seed, &y, p),
            Ok(Some((class, detail))) => {
                let min = minimize(&y, |s| {
                    ref_parse(s).is_none()
                        && matches!(no_panic(|| check_invalid(s)), Ok(Some((c, _))) if c == class)
                });
                tally.fail(&class, seed, &min, detail)
            }
            Ok(None) => {}
        }
        // repair must never panic either, and whatever it returns as
        // "changed" must be valid JSON
        match no_panic(|| fix_json_with_report(&y)) {
            Err(p) => tally.fail("PANIC in repair", seed, &y, p),
            Ok((fixed, report)) => {
                if fixed != y && ref_parse(&fixed).is_none() {
                    tally.fail(
                        "repair: changed the text but result is still invalid",
                        seed,
                        &y,
                        format!("fixed={fixed:?} report={report:?}"),
                    );
                }
            }
        }
    });
    let r = t.report("json invalid inputs", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

/// Validity verdict must agree with the independent reference reader on
/// every input, and with serde_json except for the documented deviations.
#[test]
fn fuzz_json_validator_differential() {
    let started = Instant::now();
    let n = cases(60_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let y = if rng.chance(1, 4) {
            random_soup(&mut rng, 30)
        } else {
            let o = opts(&mut rng);
            let d = gen_doc(&mut rng, &o);
            if rng.chance(1, 3) {
                d.render()
            } else {
                let c = *rng.pick(json_gen::ALL);
                match corrupt(&mut rng, &d, c) {
                    Some(d2) => d2.render(),
                    None => d.render(),
                }
            }
        };
        let ours = match no_panic(|| json_parse_error(&y)) {
            Ok(e) => e.is_empty(),
            Err(p) => return tally.fail("PANIC", seed, &y, p),
        };
        let reference = ref_parse(&y).is_some();
        if ours != reference {
            tally.fail(
                if ours {
                    "validator accepts what the reference rejects"
                } else {
                    "validator rejects what the reference accepts"
                },
                seed,
                &y,
                json_parse_error(&y),
            );
        }
        let serde = serde_json::from_str::<serde::de::IgnoredAny>(&y).is_ok();
        let deviation = y.starts_with('\u{FEFF}') || {
            let l = y.to_ascii_lowercase();
            l.contains("\\ud8")
                || l.contains("\\ud9")
                || l.contains("\\uda")
                || l.contains("\\udb")
                || l.contains("\\udc")
                || l.contains("\\udd")
                || l.contains("\\ude")
                || l.contains("\\udf")
        };
        if serde != reference && !deviation {
            tally.fail(
                "reference disagrees with serde_json",
                seed,
                &y,
                String::new(),
            );
        }
    });
    let r = t.report("json validator differential", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

/// Inject ONE known fault at a known offset; the error must point at it.
pub fn error_position_case(seed: u64, tally: &mut Tally) {
    let mut rng = Rng::new(seed);
    let o = opts(&mut rng);
    let d = gen_doc(&mut rng, &o);
    let x = d.render();
    let (outside, inside) = boundaries(&d);
    let (y, at) = match rng.below(3) {
        0 => {
            let at = *rng.pick(&outside);
            let bad = *rng.pick(&["@", "#", "\u{1}", "x", ")", "+", "'", "\u{A0}"]);
            (format!("{}{}{}", &x[..at], bad, &x[at..]), at)
        }
        1 if !inside.is_empty() => {
            let at = *rng.pick(&inside);
            let bad = *rng.pick(&["\u{1}", "\n", "\u{1F}", "\t", "\0"]);
            (format!("{}{}{}", &x[..at], bad, &x[at..]), at)
        }
        _ => {
            // drop the final closer: the error is at end of input
            let t = x.trim_end_matches([' ', '\n', '\r', '\t']);
            if !(t.ends_with(']') || t.ends_with('}')) {
                return;
            }
            let y = format!("{}{}", &t[..t.len() - 1], &x[t.len()..]);
            let at = y.len();
            (y, at)
        }
    };
    if ref_parse(&y).is_some() {
        return; // e.g. whitespace-like insertion that stayed valid
    }
    let err = match no_panic(|| json_parse_error(&y)) {
        Ok(e) => e,
        Err(p) => return tally.fail("PANIC", seed, &y, p),
    };
    let want = line_col(&y, at);
    match reported_pos(&err) {
        None => tally.fail("error without line/column", seed, &y, err),
        Some(got) if got != want => {
            let class = if got.0 != want.0 {
                "error position: wrong LINE"
            } else {
                "error position: wrong column"
            };
            tally.fail(
                class,
                seed,
                &y,
                format!("want line {} column {}, got {err:?}", want.0, want.1),
            )
        }
        _ => {}
    }
}

#[test]
fn fuzz_json_error_positions() {
    let started = Instant::now();
    let n = cases(40_000);
    let t = run_parallel(n, error_position_case);
    let r = t.report("json error positions", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

// ── repair: mutation testing ─────────────────────────────────────────────

fn is_structural(c: Corruption) -> bool {
    matches!(
        c,
        Corruption::MissingComma
            | Corruption::MissingColon
            | Corruption::ExtraCloser
            | Corruption::MissingCloserAtEnd
            | Corruption::Truncate
            | Corruption::DeleteChar
            | Corruption::InsertChar
    )
}

/// Corruptions that can leave valid JSON go first, then the always-
/// detectable structural ones, then the lexical ones.
fn order_key(c: Corruption) -> u8 {
    match c {
        Corruption::MissingColon => 1,
        c if is_structural(c) => 0,
        _ => 2,
    }
}

pub fn repair_case(seed: u64, stacked: usize, tally: &mut Tally) {
    let mut rng = Rng::new(seed);
    let o = opts(&mut rng);
    let d = gen_doc(&mut rng, &o);
    let x = d.render();
    let orig = match ref_parse(&x) {
        Some(v) => v,
        None => return tally.note("generator produced invalid doc"),
    };
    // Every meaning the text had along the way. Structural corruptions go
    // first, and those that can leave VALID JSON first of all (a truncated
    // number, two numbers fused by a lost comma, a moved closer): when that
    // happens the earlier meaning is gone from the text and no repair can
    // recover it, so the last valid state is an equally right answer. The
    // lexical corruptions applied after (comments, quotes, Python literals,
    // ...) are always detectable and must be undone exactly.
    let mut acceptable = vec![orig];
    let mut cur = d;
    let mut applied: Vec<Corruption> = Vec::new();
    let mut picks: Vec<Corruption> = (0..stacked).map(|_| *rng.pick(json_gen::ALL)).collect();
    picks.sort_by_key(|c| order_key(*c));
    for c in picks {
        if let Some(n) = corrupt(&mut rng, &cur, c) {
            cur = n;
            applied.push(c);
            if is_structural(c) {
                if let Some(v) = ref_parse(&cur.render()) {
                    acceptable.push(v);
                }
            }
        }
    }
    if applied.is_empty() {
        return;
    }
    applied.sort();
    let label: Vec<String> = applied.iter().map(|c| format!("{c:?}")).collect();
    let label = label.join("+");
    let y = cur.render();
    let (fixed, report) = match no_panic(|| fix_json_with_report(&y)) {
        Ok(r) => r,
        Err(p) => return tally.fail("PANIC in repair", seed, &y, p),
    };
    let outcome = if ref_parse(&y).is_some() {
        if fixed != y {
            tally.fail(
                "repair: touched VALID JSON",
                seed,
                &y,
                format!("[{label}] fixed={fixed:?}"),
            );
        }
        "still-valid"
    } else if fixed == y {
        if reported_pos(&report).is_none() {
            tally.fail(
                "repair: declined without a line/column",
                seed,
                &y,
                format!("[{label}] report={report:?}"),
            );
            "declined-vague"
        } else {
            "declined"
        }
    } else {
        match ref_parse(&fixed) {
            None => {
                tally.fail(
                    &format!("repair: CHANGED text, still invalid [{label}]"),
                    seed,
                    &y,
                    format!("orig={x:?}\n    fixed={fixed:?}"),
                );
                "changed-invalid"
            }
            Some(v) if !acceptable.contains(&v) => {
                // Two or more STRUCTURAL faults can compose into a shape a
                // single, different fault also explains (a closer moved =
                // missing comma; truncated after ',' plus a stray ']' =
                // trailing comma), and a random typo INSIDE a string can
                // become a catalogue fault (a deleted '\' leaves `\'`).
                // Those inputs no longer carry the original meaning; they
                // are measured and printed, not failed. Everything else
                // (any lexical faults on top of at most one structural
                // fault) must restore or decline.
                let structural = applied.iter().filter(|c| is_structural(**c)).count();
                let random_typo = structural >= 2
                    || applied
                        .iter()
                        .any(|c| matches!(c, Corruption::DeleteChar | Corruption::InsertChar));
                let class = format!("repair: MEANING CHANGED [{label}]");
                let detail = format!("orig={x:?}\n    fixed={fixed:?}");
                if random_typo {
                    tally.info(&class, seed, &y, detail);
                } else {
                    tally.fail(&class, seed, &y, detail);
                }
                "MEANING-CHANGED"
            }
            Some(_) => "restored",
        }
    };
    tally.note(&format!("{stacked}x {label} => {outcome}"));
}

fn repair_run(stacked: usize, default: u64) -> Tally {
    run_parallel(cases(default), |seed, tally| {
        repair_case(seed, stacked, tally)
    })
}

/// Summarise per-corruption outcomes of a repair run as a table.
pub fn outcome_table(t: &Tally, stacked: usize) -> String {
    use std::collections::BTreeMap;
    let mut rows: BTreeMap<String, [u64; 5]> = BTreeMap::new();
    let prefix = format!("{stacked}x ");
    for (k, v) in &t.counts {
        if let Some(rest) = k.strip_prefix(&prefix) {
            if let Some((label, outcome)) = rest.split_once(" => ") {
                let key = if stacked == 1 {
                    label.to_string()
                } else {
                    "(all stacked)".to_string()
                };
                let row = rows.entry(key).or_insert([0; 5]);
                let i = match outcome {
                    "restored" => 0,
                    "declined" | "declined-vague" => 1,
                    "MEANING-CHANGED" => 2,
                    "changed-invalid" => 3,
                    _ => 4,
                };
                row[i] += v;
            }
        }
    }
    let mut s = format!(
        "{:<28} {:>9} {:>9} {:>9} {:>9} {:>9}\n",
        "corruption", "restored", "declined", "MEANING", "chg-inval", "valid"
    );
    for (k, r) in rows {
        s.push_str(&format!(
            "{:<28} {:>9} {:>9} {:>9} {:>9} {:>9}\n",
            k, r[0], r[1], r[2], r[3], r[4]
        ));
    }
    s
}

#[test]
fn fuzz_json_repair_single_corruption() {
    let started = Instant::now();
    let t = repair_run(1, 40_000);
    let r = t.report("json repair, 1 corruption", started);
    println!("{}\n{r}", outcome_table(&t, 1));
    assert!(t.examples.is_empty(), "{r}");
}

#[test]
fn fuzz_json_repair_stacked_corruptions() {
    let started = Instant::now();
    let mut t = repair_run(2, 20_000);
    t.merge(repair_run(3, 20_000));
    let r = t.report("json repair, 2 and 3 stacked corruptions", started);
    println!("{}\n{}\n{r}", outcome_table(&t, 2), outcome_table(&t, 3));
    assert!(t.examples.is_empty(), "{r}");
}

// ── pathological shapes: no panic, no stack overflow, linear time ──────────

fn timed<T>(label: &str, budget: Duration, f: impl FnOnce() -> T) -> T {
    let t0 = Instant::now();
    let r = f();
    let el = t0.elapsed();
    // Budgets are generous on purpose: these shapes are 1-8 MB, so quadratic
    // behaviour would take minutes, while a loaded CI runner may be several
    // times slower than a workstation. Debug builds are ~10x slower again.
    let budget = if cfg!(debug_assertions) {
        budget * 15
    } else {
        budget
    };
    assert!(el < budget, "{label}: took {el:?} (budget {budget:?})");
    println!("{label}: {el:?}");
    r
}

#[test]
fn json_pathological_shapes() {
    let deep_open = "[".repeat(1_000_000);
    timed("1M unclosed '['", Duration::from_secs(15), || {
        assert!(!json_parse_error(&deep_open).is_empty());
        assert!(format_json(&deep_open, 4) == deep_open, "format changed");
        assert!(minify_json(&deep_open) == deep_open, "minify changed");
        let (f, rep) = fix_json_with_report(&deep_open);
        assert!(f == deep_open, "repair changed it");
        assert!(!rep.is_empty());
    });
    let deep = format!("{}{}", "[".repeat(100_000), "]".repeat(100_000));
    timed("100k-deep valid array", Duration::from_secs(15), || {
        assert_eq!(json_parse_error(&deep), "");
        assert!(minify_json(&deep) == deep, "minify changed");
        // Pretty-printing 100k levels would be ~20 GB of indentation; the
        // formatter must refuse cleanly rather than allocate it.
        let f = format_json(&deep, 4);
        assert!(f == deep || f.len() < 512 * 1024 * 1024);
        let (fixed, _) = fix_json_with_report(&deep);
        assert!(fixed == deep, "repair changed valid JSON");
    });
    let deep_obj = format!("{}1{}", "{\"a\":".repeat(20_000), "}".repeat(20_000));
    timed("20k-deep valid object", Duration::from_secs(15), || {
        assert_eq!(json_parse_error(&deep_obj), "");
        let _ = format_json(&deep_obj, 2);
    });
    let big_str = format!("[\"{}\"]", "abc\\n\u{e9}".repeat(1_000_000));
    timed("6 MB string", Duration::from_secs(15), || {
        assert_eq!(json_parse_error(&big_str), "");
        let f = format_json(&big_str, 2);
        assert!(ref_tokens(&f) == ref_tokens(&big_str), "string changed");
    });
    let big_num = format!("[{}]", "7".repeat(5_000_000));
    timed("5 MB number", Duration::from_secs(15), || {
        assert_eq!(json_parse_error(&big_num), "");
        let f = format_json(&big_num, 2);
        assert!(f.contains(&"7".repeat(5_000_000)), "number text lost");
    });
    let mut doc = String::from("[");
    let mut i = 0u64;
    while doc.len() < 8_000_000 {
        if i > 0 {
            doc.push(',');
        }
        doc.push_str(&format!(
            "{{\"id\":{i},\"name\":\"item {i}\",\"tags\":[\"a\",\"b\"],\"v\":{i}.5e-3,\"ok\":true}}"
        ));
        i += 1;
    }
    doc.push(']');
    timed("8 MB document", Duration::from_secs(15), || {
        assert_eq!(json_parse_error(&doc), "");
        let f = format_json(&doc, 4);
        assert!(minify_json(&f) == doc, "round trip changed");
    });
    let mut broken = doc.clone();
    broken.insert(4_000_000, '@');
    timed("8 MB document, one fault", Duration::from_secs(15), || {
        assert!(!json_parse_error(&broken).is_empty());
        assert!(format_json(&broken, 4) == broken, "format changed invalid");
        let (f, rep) = fix_json_with_report(&broken);
        assert!(f == broken, "{}", &rep[..rep.len().min(300)]);
    });
    let commas = format!("[{}]", "1,".repeat(2_000_000));
    timed(
        "2M elements + trailing comma",
        Duration::from_secs(15),
        || {
            let (f, _) = fix_json_with_report(&commas);
            assert!(f.len() == commas.len() - 1 || f == commas);
        },
    );
    let quotes = "'".repeat(3_000_000);
    timed("3M single quotes", Duration::from_secs(15), || {
        let _ = fix_json_with_report(&quotes);
    });
    let keys = format!("{{{}}}", "a:1,".repeat(1_000_000));
    timed("1M unquoted keys", Duration::from_secs(15), || {
        let _ = fix_json_with_report(&keys);
    });
    let comments = "/*".repeat(1_000_000);
    timed("1M '/*'", Duration::from_secs(15), || {
        let _ = fix_json_with_report(&comments);
        let _ = format_json(&comments, 2);
    });
}

/// Arbitrary bytes, NULs, lone surrogates, BOMs, CRLF: never panic.
#[test]
fn fuzz_json_soup_never_panics() {
    let started = Instant::now();
    let n = cases(60_000);
    let t = run_parallel(n, |seed, tally| {
        let mut rng = Rng::new(seed);
        let y = random_soup(&mut rng, 60);
        let r = no_panic(|| {
            let _ = json_parse_error(&y);
            let _ = format_json(&y, rng.below(9));
            let _ = minify_json(&y);
            let _ = fix_json_with_report(&y);
        });
        if let Err(p) = r {
            let min = minimize(&y, |s| {
                no_panic(|| {
                    let _ = json_parse_error(s);
                    let _ = format_json(s, 4);
                    let _ = minify_json(s);
                    let _ = fix_json_with_report(s);
                })
                .is_err()
            });
            tally.fail("PANIC", seed, &min, p);
        }
    });
    let r = t.report("json soup", started);
    println!("{r}");
    assert!(t.examples.is_empty(), "{r}");
}

/// Diagnostic (ignored): which single random typos does the repairer turn
/// into a different valid document, and with which repair?
#[test]
#[ignore]
fn diag_random_typo_meaning_changes() {
    use std::collections::BTreeMap;
    let mut hist: BTreeMap<String, (u64, String)> = BTreeMap::new();
    let base = super::base_seed();
    let (mut total, mut changed, mut declined) = (0u64, 0u64, 0u64);
    for i in 0..cases(300_000) {
        let mut rng = Rng::new(super::case_seed(base, i));
        let o = opts(&mut rng);
        let d = gen_doc(&mut rng, &o);
        let x = d.render();
        let c = *rng.pick(&[Corruption::InsertChar, Corruption::DeleteChar]);
        let y = match corrupt(&mut rng, &d, c) {
            Some(d2) => d2.render(),
            None => continue,
        };
        if ref_parse(&y).is_some() {
            continue;
        }
        total += 1;
        let (fixed, rep) = fix_json_with_report(&y);
        if fixed == y {
            declined += 1;
        } else if ref_parse(&fixed) != ref_parse(&x) {
            changed += 1;
            let what: String = rep
                .lines()
                .nth(2)
                .and_then(|l| l.split(": ").nth(1))
                .unwrap_or("")
                .to_string();
            let p = x
                .bytes()
                .zip(y.bytes())
                .position(|(a, b)| a != b)
                .unwrap_or(0);
            let src = if c == Corruption::InsertChar { &y } else { &x };
            let ch = src[p..].chars().next().unwrap_or(' ');
            let key = format!("{c:?} {ch:?} -> {what}");
            let e = hist.entry(key).or_insert((0, y.clone()));
            e.0 += 1;
            if y.len() < e.1.len() {
                e.1 = y.clone();
            }
        }
    }
    println!("{total} invalid random typos: {declined} declined, {changed} turned into a different valid document");
    for (k, (n, ex)) in hist {
        let ex: String = ex.chars().take(120).collect();
        println!("{n:6}  {k}\n        e.g. {ex:?}");
    }
}
