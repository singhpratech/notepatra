// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.128 — Password Generator core.
//
// Pure Qt Core: no widgets, no I/O, no network. Everything here is a
// free function over Options so test_passwordgen.cpp can exercise the
// character maths and the RNG without a QApplication.
//
// Randomness comes from QRandomGenerator::system(), Qt's cryptographic
// generator — never the seeded global() one.
//
// Be precise about where those bytes come from, because it is not what
// the obvious reading suggests: on x86-64 Qt fills from the CPU's RDRAND
// instruction first, and only falls back to the OS entropy source
// (getentropy/getrandom on Linux, RtlGenRandom on Windows) for whatever
// RDRAND declines to supply. Measured here: ~7 million draws issued
// exactly zero getrandom syscalls. Anyone who needs the CPU RNG out of
// their threat model has to bypass Qt — there is no way to configure it.
//
// Draws use rejection sampling, so no character is more likely than
// another just because the alphabet size does not divide 2^32.
//
// Nothing in this file writes to disk. Generated values exist only in
// the caller's memory; Qt strings are copy-on-write and cannot be
// reliably wiped, so this is deliberately NOT described as "secure
// memory" anywhere in the UI.

#pragma once

#include <QChar>
#include <QString>
#include <QStringList>

namespace PasswordGen {

// Character classes a random password can draw from. Values are a
// bitmask so Options::classes round-trips through a single int.
enum CharClass {
    Lower   = 1 << 0,
    Upper   = 1 << 1,
    Digits  = 1 << 2,
    Symbols = 1 << 3,
    Extra   = 1 << 4,  // set implicitly when Options::extra is non-empty
};

enum class Mode { Characters, Passphrase };

struct Options {
    Mode mode = Mode::Characters;

    // ── Characters mode ────────────────────────────────────────────
    int     length            = 20;
    int     classes           = Lower | Upper | Digits | Symbols;
    QString extra;                     // user-supplied additional characters
    bool    excludeLookalikes = false; // drop 0 O 1 l I
    bool    requireEachClass  = true;  // >= 1 char from every selected class

    // ── Passphrase mode ────────────────────────────────────────────
    int   words          = 6;
    QChar separator      = QLatin1Char('-');
    bool  capitalise     = false;      // Correct-Horse-Battery
    bool  appendDigits   = false;      // ...-47

    bool operator==(const Options &o) const;
};

// Hard limits. The UI clamps to these and validate() enforces them.
constexpr int kMinLength = 4;
constexpr int kMaxLength = 256;
constexpr int kMinWords  = 3;
constexpr int kMaxWords  = 24;
constexpr int kMaxCount  = 100;   // bulk generate

// The five class alphabets, after look-alike removal is applied by
// alphabet(). Exposed so the UI can show "26 letters" style counts and
// so tests can assert disjointness.
QString classChars(CharClass c, bool excludeLookalikes);

// The exact alphabet Options resolves to: selected classes concatenated,
// de-duplicated, look-alikes removed, Options::extra sanitised (control
// characters and whitespace stripped) and appended. Empty when nothing
// is selectable.
QString alphabet(const Options &o);

// The selected classes as DISJOINT alphabets, in bitmask order. A
// character that appears in two classes (only possible via Options::extra)
// is assigned to the first class that claims it, so the pieces partition
// alphabet(). Empty pieces are omitted.
QStringList disjointClasses(const Options &o);

// Entropy of the generator in bits — log2 of the number of distinct
// values it can emit, NOT a guess at how hard the string looks.
//
// Characters mode with requireEachClass off:  length * log2(|alphabet|).
// Characters mode with requireEachClass on:   log2 of the count of
//   length-N strings containing at least one character from every
//   selected class, via inclusion-exclusion over the class subsets. That
//   is strictly LESS than length * log2(|alphabet|), which is why the
//   number drops slightly when the checkbox is ticked.
// Passphrase mode: words * log2(wordlistSize()) [+ log2(100) when
//   appendDigits appends a two-digit group].
double entropyBits(const Options &o);

// Probability that one naive draw satisfies requireEachClass. 1.0 when
// the constraint is off or vacuous. Drives both the retry budget in
// generate() and the "too short for these classes" message.
double acceptanceProbability(const Options &o);

struct Strength {
    QString label;  // "Very weak" … "Excellent"
    int     score;  // 0..4, for the meter
};
Strength strength(double bits);

// Human-readable reason these Options cannot produce a password, or an
// empty string when they can. Callers must check this before generate().
QString validate(const Options &o);

// One password. Returns an empty string if validate() would fail.
QString generate(const Options &o);

// `count` independent passwords (clamped to kMaxCount).
QStringList generateMany(const Options &o, int count);

// Uniform value in [0, n) drawn from the system CSPRNG with rejection
// sampling. Exposed for tests. n == 0 returns 0.
quint32 uniformBelow(quint32 n);

// Wordlist size, derived from the generated header — never hardcoded.
int wordlistSize();
QString wordAt(int i);

}  // namespace PasswordGen
