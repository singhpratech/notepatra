// SPDX-License-Identifier: GPL-3.0-or-later

//! Property / fuzz harness for the JSON tools and the bracket fixer.
//!
//! Test-only (`#[cfg(test)]`): nothing here reaches the shipped static lib,
//! and it adds no dependency — the generator is a seeded xorshift.
//!
//! Every case is reproducible from one number. Case `i` of a run uses
//! `case_seed(base, i)`, where `base` comes from `NP_FUZZ_SEED` (default
//! fixed), so a failure prints the seed and a minimized input, and
//! `NP_FUZZ_SEED=<seed> NP_FUZZ_CASES=1` replays exactly that case.
//!
//! Case counts: `NP_FUZZ_CASES` scales every target (default keeps the whole
//! `cargo test --release` run well under 30 s). The long run is
//!
//! ```text
//! cd rust-core && NP_FUZZ_CASES=5000000 cargo test --release fuzz_ -- --nocapture --test-threads=8
//! ```

pub mod bracket_gen;
pub mod bracket_props;
pub mod json_gen;
pub mod json_props;
pub mod json_ref;
mod regressions;

use std::collections::BTreeMap;
use std::time::Instant;

/// xorshift64* — tiny, fast, deterministic.
#[derive(Clone)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        // splitmix the seed so neighbouring seeds diverge immediately.
        let mut z = seed.wrapping_add(0x9E37_79B9_7F4A_7C15);
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^= z >> 31;
        Rng(if z == 0 { 0x1234_5678 } else { z })
    }
    pub fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545_F491_4F6C_DD1D)
    }
    /// Uniform in `0..n` (n > 0).
    pub fn below(&mut self, n: usize) -> usize {
        (self.next() % (n as u64)) as usize
    }
    pub fn chance(&mut self, num: u32, den: u32) -> bool {
        (self.next() % den as u64) < num as u64
    }
    pub fn pick<'a, T>(&mut self, xs: &'a [T]) -> &'a T {
        &xs[self.below(xs.len())]
    }
}

pub fn base_seed() -> u64 {
    std::env::var("NP_FUZZ_SEED")
        .ok()
        .and_then(|s| s.trim().parse().ok())
        .unwrap_or(0x4E50_4655_5A5A)
}

pub fn case_seed(base: u64, i: u64) -> u64 {
    base ^ i.wrapping_mul(0x9E37_79B9_7F4A_7C15)
}

/// Number of cases for a target. `default` is the per-target count of the
/// quick run; `NP_FUZZ_CASES` overrides every target.
pub fn cases(default: u64) -> u64 {
    std::env::var("NP_FUZZ_CASES")
        .ok()
        .and_then(|s| s.trim().replace('_', "").parse().ok())
        .unwrap_or(default)
}

/// Greedy delta-debugging over chars: repeatedly drop chunks (halving the
/// chunk size) while `still_fails` keeps returning true.
pub fn minimize(input: &str, mut still_fails: impl FnMut(&str) -> bool) -> String {
    let mut cur: Vec<char> = input.chars().collect();
    if cur.len() > 20_000 {
        return input.to_string(); // too big to minimize char-by-char
    }
    let mut chunk = (cur.len() / 2).max(1);
    let mut budget = 20_000usize;
    loop {
        let mut i = 0;
        let mut progressed = false;
        while i < cur.len() && budget > 0 {
            budget -= 1;
            let end = (i + chunk).min(cur.len());
            let cand: Vec<char> = cur[..i].iter().chain(cur[end..].iter()).copied().collect();
            let s: String = cand.iter().collect();
            if still_fails(&s) {
                cur = cand;
                progressed = true;
            } else {
                i += chunk;
            }
        }
        if budget == 0 || (chunk == 1 && !progressed) {
            break;
        }
        if !progressed {
            chunk = (chunk / 2).max(1);
        }
    }
    cur.into_iter().collect()
}

/// Collects failures by class; keeps the first (minimized) example of each.
#[derive(Default)]
pub struct Tally {
    pub counts: BTreeMap<String, u64>,
    pub examples: BTreeMap<String, (u64, String, String)>,
    /// Measured-but-accepted outcomes (printed, never asserted).
    pub infos: BTreeMap<String, (u64, String, String)>,
    pub cases: u64,
}

fn keep_shortest(
    m: &mut BTreeMap<String, (u64, String, String)>,
    class: &str,
    v: (u64, String, String),
) {
    let keep = match m.get(class) {
        Some((_, old, _)) => v.1.len() < old.len(),
        None => true,
    };
    if keep {
        m.insert(class.to_string(), v);
    }
}

impl Tally {
    pub fn fail(&mut self, class: &str, seed: u64, input: &str, detail: String) {
        *self.counts.entry(class.to_string()).or_insert(0) += 1;
        // Keep the SHORTEST example of each class: with many random cases
        // the shortest one is usually close to minimal already.
        keep_shortest(&mut self.examples, class, (seed, input.to_string(), detail));
    }
    pub fn info(&mut self, class: &str, seed: u64, input: &str, detail: String) {
        *self.counts.entry(class.to_string()).or_insert(0) += 1;
        keep_shortest(&mut self.infos, class, (seed, input.to_string(), detail));
    }
    pub fn note(&mut self, class: &str) {
        *self.counts.entry(class.to_string()).or_insert(0) += 1;
    }
    pub fn merge(&mut self, o: Tally) {
        self.cases += o.cases;
        for (k, v) in o.counts {
            *self.counts.entry(k).or_insert(0) += v;
        }
        for (k, v) in o.examples {
            keep_shortest(&mut self.examples, &k, v);
        }
        for (k, v) in o.infos {
            keep_shortest(&mut self.infos, &k, v);
        }
    }
    pub fn report(&self, title: &str, started: Instant) -> String {
        let mut s = format!(
            "== {title}: {} cases in {:.2?} ==\n",
            self.cases,
            started.elapsed()
        );
        for (k, v) in &self.counts {
            s.push_str(&format!("  {k}: {v}\n"));
        }
        for (k, (seed, input, detail)) in &self.examples {
            let shown: String = input.chars().take(400).collect();
            s.push_str(&format!(
                "  FAIL {k}\n    seed={seed}\n    input={shown:?}\n    {detail}\n"
            ));
        }
        for (k, (seed, input, detail)) in &self.infos {
            let shown: String = input.chars().take(400).collect();
            s.push_str(&format!(
                "  MEASURED {k}\n    seed={seed}\n    input={shown:?}\n    {detail}\n"
            ));
        }
        s
    }
}

/// Run `per_case(seed, &mut tally)` for `n` cases split over the available
/// cores (deterministic per case; only the order of tally merging varies).
pub fn run_parallel(n: u64, per_case: impl Fn(u64, &mut Tally) + Sync) -> Tally {
    let base = base_seed();
    let threads = std::thread::available_parallelism()
        .map(|n| n.get())
        .unwrap_or(4)
        .clamp(1, 6) as u64;
    let per_case = &per_case;
    let mut total = Tally::default();
    std::thread::scope(|sc| {
        let hs: Vec<_> = (0..threads)
            .map(|t| {
                sc.spawn(move || {
                    let mut tally = Tally::default();
                    let mut i = t;
                    while i < n {
                        per_case(case_seed(base, i), &mut tally);
                        tally.cases += 1;
                        i += threads;
                    }
                    tally
                })
            })
            .collect();
        for h in hs {
            total.merge(h.join().expect("fuzz worker panicked"));
        }
    });
    total
}

/// Random text from a byte soup biased towards JSON / code punctuation —
/// the "arbitrary bytes" input (decoded lossily, as the C++ side always
/// hands Rust valid UTF-8).
pub fn random_soup(rng: &mut Rng, max_len: usize) -> String {
    const ALPHA: &[&str] = &[
        "{", "}", "[", "]", "(", ")", "\"", "'", ",", ":", " ", "\n", "\r\n", "\t", "\\", "/", "*",
        "#", "`", "$", "-", "+", ".", "0", "1", "9", "e", "E", "a", "true", "false", "null",
        "True", "None", "NaN", "\u{FEFF}", "\u{201C}", "\u{201D}", "\u{2018}", "\u{2019}", "\0",
        "\u{1}", "é", "😀", "\u{7F}", "u", "\\u", "\\ud800", "x", "//", "/*", "*/", "<<", "EOF",
        "\"\"\"",
    ];
    let len = rng.below(max_len + 1);
    let mut s = String::new();
    for _ in 0..len {
        if rng.chance(1, 8) {
            // raw random bytes, lossily decoded
            let n = 1 + rng.below(4);
            let bytes: Vec<u8> = (0..n).map(|_| rng.next() as u8).collect();
            s.push_str(&String::from_utf8_lossy(&bytes));
        } else {
            s.push_str(rng.pick(ALPHA));
        }
    }
    s
}
