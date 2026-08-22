// SPDX-License-Identifier: GPL-3.0-or-later

#include "passwordgen_core.h"
#include "passwordgen_wordlist.h"

#include <QRandomGenerator>

#include <notepad_core.h>

#include <cmath>
#include <cstring>

namespace PasswordGen {

namespace {

// Symbols deliberately exclude the quote characters, the backslash, the
// backtick and the pipe: those are the ones that break when a password
// is pasted into a shell command, a YAML file or a connection string,
// and every one of them has a safer twin already in the set.
constexpr const char *kLowerAll   = "abcdefghijklmnopqrstuvwxyz";
constexpr const char *kUpperAll   = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr const char *kDigitsAll  = "0123456789";
constexpr const char *kSymbolsAll = "!#$%&()*+,-./:;<=>?@[]^_{}~";

// Glyph pairs that get misread off a screen or a printout.
constexpr const char *kLookalikes = "0O1lI";

QString stripLookalikes(QString s, bool strip) {
    if (!strip) return s;
    for (const QChar c : QString::fromLatin1(kLookalikes)) s.remove(c);
    return s;
}

// Order matters: it is the order disjointClasses() partitions in, and
// the order alphabet() concatenates in.
constexpr CharClass kOrder[] = { Lower, Upper, Digits, Symbols };

QString sanitiseExtra(const QString &in) {
    QString out;
    out.reserve(in.size());
    for (const QChar c : in) {
        // Whitespace is invisible in a password field and control
        // characters cannot survive a copy/paste round trip.
        if (c.isSpace() || c.category() == QChar::Other_Control) continue;
        // Format characters are worse than useless in a password: a
        // zero-width space or a soft hyphen is invisible, so the user
        // cannot retype what they were given.
        if (c.category() == QChar::Other_Format) continue;
        // QString iterates UTF-16 code units. A character outside the
        // BMP arrives as two surrogates, and drawing them independently
        // would emit unpaired halves — a string that is not valid text
        // and corrupts on any UTF-8 round trip. Drop them rather than
        // pretend to support astral characters.
        if (c.isSurrogate() || c.category() == QChar::Other_NotAssigned) continue;
        if (!out.contains(c)) out.append(c);
    }
    return out;
}

}  // namespace

bool Options::operator==(const Options &o) const {
    return mode == o.mode && length == o.length && classes == o.classes &&
           extra == o.extra && excludeLookalikes == o.excludeLookalikes &&
           requireEachClass == o.requireEachClass && words == o.words &&
           separator == o.separator && capitalise == o.capitalise &&
           appendDigits == o.appendDigits;
}

QString classChars(CharClass c, bool excludeLookalikes) {
    switch (c) {
    case Lower:   return stripLookalikes(QString::fromLatin1(kLowerAll),   excludeLookalikes);
    case Upper:   return stripLookalikes(QString::fromLatin1(kUpperAll),   excludeLookalikes);
    case Digits:  return stripLookalikes(QString::fromLatin1(kDigitsAll),  excludeLookalikes);
    case Symbols: return stripLookalikes(QString::fromLatin1(kSymbolsAll), excludeLookalikes);
    case Extra:   break;
    }
    return QString();
}

QStringList disjointClasses(const Options &o) {
    QStringList parts;
    QString seen;
    auto claim = [&](QString chars) {
        QString mine;
        for (const QChar c : chars) {
            if (seen.contains(c)) continue;
            seen.append(c);
            mine.append(c);
        }
        if (!mine.isEmpty()) parts.append(mine);
    };
    for (const CharClass c : kOrder)
        if (o.classes & c) claim(classChars(c, o.excludeLookalikes));
    // Extra is implicit: any non-empty custom set participates, and the
    // look-alike filter applies to it too so the checkbox means one thing.
    claim(stripLookalikes(sanitiseExtra(o.extra), o.excludeLookalikes));
    return parts;
}

QString alphabet(const Options &o) {
    return disjointClasses(o).join(QString());
}

void fillRandom(unsigned char *buf, int n) {
    if (!buf || n <= 0) return;
    // One door for every draw in this file: the OS source first, Qt only
    // when the OS source refuses.
    if (npc_random_bytes(buf, size_t(n)) == 1) return;
    QRandomGenerator::system()->generate(
        reinterpret_cast<quint32 *>(buf),
        reinterpret_cast<quint32 *>(buf + (n / 4) * 4));
    // generate(begin,end) fills whole 32-bit words only; top up the tail.
    for (int i = (n / 4) * 4; i < n; ++i)
        buf[i] = static_cast<unsigned char>(QRandomGenerator::system()->generate() & 0xFF);
}

quint32 uniformBelow(quint32 n) {
    if (n <= 1) return 0;
    // Accept only from the largest prefix of [0, 2^32) whose size is a
    // multiple of n; anything above it would make small remainders more
    // likely than large ones.
    constexpr quint64 kRange = quint64(1) << 32;
    const quint64 limit = kRange - (kRange % n);
    // The rejection region is at most n values out of 2^32, so a working
    // generator clears this on the first try with overwhelming odds. The
    // cap exists only so a wedged RNG cannot spin the UI forever; taking
    // the modulo after 1024 rejections is a bias no one can observe, and
    // a hang is worse than one that no one can.
    quint32 x = 0;
    for (int i = 0; i < 1024; ++i) {
        unsigned char b[4];
        fillRandom(b, 4);
        std::memcpy(&x, b, sizeof(x));
        if (quint64(x) < limit) break;
    }
    return x % n;
}

int wordlistSize() { return kWordlistCount; }

QString wordAt(int i) {
    if (i < 0 || i >= kWordlistCount) return QString();
    return QString::fromLatin1(kWordlist[i]);
}

double acceptanceProbability(const Options &o) {
    if (o.mode != Mode::Characters || !o.requireEachClass) return 1.0;
    const QStringList parts = disjointClasses(o);
    const int k = parts.size();
    if (k <= 1) return 1.0;  // every string satisfies "one of the only class"
    const double total = alphabet(o).size();
    if (total <= 0 || o.length <= 0) return 0.0;

    // Inclusion-exclusion over which classes are FORBIDDEN. Kept as
    // ratios rather than raw counts so |A|^length never overflows.
    double sum = 0.0;
    for (int mask = 0; mask < (1 << k); ++mask) {
        int removed = 0, bits = 0;
        for (int i = 0; i < k; ++i) {
            if (mask & (1 << i)) { removed += parts.at(i).size(); ++bits; }
        }
        const double term = std::pow((total - removed) / total, double(o.length));
        sum += (bits % 2) ? -term : term;
    }
    if (sum < 0.0) return 0.0;
    if (sum > 1.0) return 1.0;
    return sum;
}

double entropyBits(const Options &o) {
    if (o.mode == Mode::Passphrase) {
        const int n = wordlistSize();
        if (n < 2 || o.words <= 0) return 0.0;
        double bits = double(o.words) * std::log2(double(n));
        if (o.appendDigits) bits += std::log2(100.0);
        return bits;
    }

    const double total = alphabet(o).size();
    if (total < 2 || o.length <= 0) return 0.0;
    const double base = double(o.length) * std::log2(total);

    const double accept = acceptanceProbability(o);
    if (accept <= 0.0) return 0.0;
    // Constraining the output to a subset of all strings can only remove
    // possibilities, so this term is <= 0.
    return base + std::log2(accept);
}

Strength strength(double bits) {
    if (bits < 40)  return { QStringLiteral("Very weak"), 0 };
    if (bits < 60)  return { QStringLiteral("Weak"),      1 };
    if (bits < 80)  return { QStringLiteral("Fair"),      2 };
    if (bits < 112) return { QStringLiteral("Strong"),    3 };
    return { QStringLiteral("Excellent"), 4 };
}

// Below this, rejection sampling would need more than ~1000 tries on
// average. validate() turns it into an actionable message instead of a
// slow generate().
static constexpr double kMinAcceptance = 1e-3;

QString validate(const Options &o) {
    if (o.mode == Mode::Passphrase) {
        if (o.words < kMinWords)
            return QStringLiteral("Use at least %1 words.").arg(kMinWords);
        if (o.words > kMaxWords)
            return QStringLiteral("Use at most %1 words.").arg(kMaxWords);
        if (wordlistSize() < 2)
            return QStringLiteral("The built-in wordlist is unavailable.");
        return QString();
    }

    if (o.length < kMinLength)
        return QStringLiteral("Length must be at least %1.").arg(kMinLength);
    if (o.length > kMaxLength)
        return QStringLiteral("Length must be at most %1.").arg(kMaxLength);

    const QStringList parts = disjointClasses(o);
    if (parts.isEmpty())
        return QStringLiteral("Pick at least one character set.");
    if (alphabet(o).size() < 2)
        return QStringLiteral("The selected character set has only one character.");

    if (o.requireEachClass && parts.size() > 1) {
        if (o.length < parts.size()) {
            return QStringLiteral("Length must be at least %1 — one character for "
                                  "each of the %1 sets in use (your custom "
                                  "characters count as one).")
                       .arg(parts.size());
        }
        if (acceptanceProbability(o) < kMinAcceptance) {
            return QStringLiteral("Length %1 is too short to reliably include one "
                                  "character from each of the %2 selected sets — "
                                  "raise the length or untick \"At least one from each set\".")
                       .arg(o.length).arg(parts.size());
        }
    }
    return QString();
}

QString generate(const Options &o) {
    if (!validate(o).isEmpty()) return QString();

    if (o.mode == Mode::Passphrase) {
        const int n = wordlistSize();
        QStringList picked;
        picked.reserve(o.words);
        for (int i = 0; i < o.words; ++i) {
            QString w = wordAt(int(uniformBelow(quint32(n))));
            if (o.capitalise && !w.isEmpty()) w[0] = w.at(0).toUpper();
            picked.append(w);
        }
        const QString sep = o.separator.isNull() ? QString() : QString(o.separator);
        QString out = picked.join(sep);
        if (o.appendDigits) {
            out += sep + QStringLiteral("%1").arg(int(uniformBelow(100)), 2, 10, QLatin1Char('0'));
        }
        return out;
    }

    const QString alpha = alphabet(o);
    const QStringList parts = disjointClasses(o);
    const bool constrained = o.requireEachClass && parts.size() > 1;

    // Rejection, not "place one of each then shuffle": placement makes
    // strings with exactly one rare-class character more likely than the
    // rest, which would make entropyBits() a lie. validate() has already
    // guaranteed the acceptance rate is workable.
    const int budget = constrained ? 100000 : 1;
    for (int attempt = 0; attempt < budget; ++attempt) {
        QString out;
        out.reserve(o.length);
        for (int i = 0; i < o.length; ++i)
            out.append(alpha.at(int(uniformBelow(quint32(alpha.size())))));
        if (!constrained) return out;

        bool complete = true;
        for (const QString &part : parts) {
            bool found = false;
            for (const QChar c : out) {
                if (part.contains(c)) { found = true; break; }
            }
            if (!found) { complete = false; break; }
        }
        if (complete) return out;
    }
    return QString();
}

QStringList generateMany(const Options &o, int count) {
    QStringList out;
    const int n = qBound(1, count, kMaxCount);
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const QString p = generate(o);
        if (p.isEmpty()) return QStringList();
        out.append(p);
    }
    return out;
}

}  // namespace PasswordGen
