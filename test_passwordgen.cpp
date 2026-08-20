/**
 * Deep test for the Password Generator core — the alphabet maths, the
 * entropy accounting, and the CSPRNG draw. No widgets and no event loop,
 * so this runs identically on every platform and in CI.
 *
 * The statistical checks are deliberately loose (6-sigma bounds over
 * large samples) so a correct generator can never fail them by chance;
 * they exist to catch a *broken* generator, not to grade randomness.
 */

#include "src/passwordgen_core.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QString>

#include <cmath>
#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", what,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++g_fail;
    }
}

using namespace PasswordGen;

static bool nearly(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps * qMax(1.0, qMax(std::fabs(a), std::fabs(b)));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    std::printf("=== Password Generator core tests ===\n\n");

    // ── wordlist integrity ─────────────────────────────────────────
    std::printf("— wordlist ───────────────────────────────────────\n");
    {
        const int n = wordlistSize();
        check("wordlist is non-trivial", n >= 1024, QString::number(n));
        check("wordlist size is a power of two (whole bits per word)",
              n > 0 && (n & (n - 1)) == 0, QString::number(n));

        QSet<QString> seen;
        bool allShape = true, allUnique = true;
        QString badShape;
        for (int i = 0; i < n; ++i) {
            const QString w = wordAt(i);
            if (seen.contains(w)) { allUnique = false; badShape = w; break; }
            seen.insert(w);
            if (w.size() < 4 || w.size() > 6) { allShape = false; badShape = w; break; }
            for (const QChar c : w) {
                if (c < QLatin1Char('a') || c > QLatin1Char('z')) {
                    allShape = false; badShape = w; break;
                }
            }
            if (!allShape) break;
        }
        check("every word is unique", allUnique, badShape);
        check("every word is 4-6 lowercase a-z", allShape, badShape);
        check("wordAt() is bounds-checked", wordAt(-1).isEmpty() && wordAt(n).isEmpty());
    }

    // ── uniformBelow ───────────────────────────────────────────────
    std::printf("\n— uniformBelow (rejection-sampled CSPRNG draw) ────\n");
    {
        check("n == 0 is 0", uniformBelow(0) == 0);
        check("n == 1 is 0", uniformBelow(1) == 0);

        bool inRange = true;
        for (quint32 n = 2; n <= 40 && inRange; ++n)
            for (int i = 0; i < 2000; ++i)
                if (uniformBelow(n) >= n) { inRange = false; break; }
        check("never returns a value >= n (n = 2..40)", inRange);

        // 6-sigma envelope: a fair 7-way draw over 70000 trials has
        // mean 10000, sd ~= 92.6, so +/- 560 is unreachable by chance.
        const int N = 70000, K = 7;
        QVector<int> hist(K, 0);
        for (int i = 0; i < N; ++i) hist[int(uniformBelow(K))]++;
        int worst = 0;
        for (int i = 0; i < K; ++i) worst = qMax(worst, qAbs(hist[i] - N / K));
        check("draws are uniform within 6 sigma", worst < 560, QString::number(worst));

        bool varies = false;
        const quint32 first = uniformBelow(1u << 30);
        for (int i = 0; i < 20 && !varies; ++i)
            if (uniformBelow(1u << 30) != first) varies = true;
        check("consecutive draws differ (not a stuck generator)", varies);
    }

    // ── alphabet composition ───────────────────────────────────────
    std::printf("\n— alphabet ───────────────────────────────────────\n");
    {
        const QString sym = classChars(Symbols, false);
        check("symbols set is 27 characters", sym.size() == 27, QString::number(sym.size()));
        bool shellSafe = true;
        for (const QChar c : QStringLiteral("'\"\\`| ")) if (sym.contains(c)) shellSafe = false;
        check("symbols exclude quote, backslash, backtick, pipe and space", shellSafe, sym);

        check("lower is 26", classChars(Lower, false).size() == 26);
        check("upper is 26", classChars(Upper, false).size() == 26);
        check("digits is 10", classChars(Digits, false).size() == 10);
        check("Extra has no intrinsic characters", classChars(Extra, false).isEmpty());

        check("look-alike filter drops exactly 0 O 1 l I",
              classChars(Lower, true).size() == 25 &&   // loses l
              classChars(Upper, true).size() == 24 &&   // loses O and I
              classChars(Digits, true).size() == 8 &&
              classChars(Symbols, true).size() == 27);

        Options o;
        check("default alphabet is 89 characters",
              alphabet(o).size() == 89, QString::number(alphabet(o).size()));
        check("default resolves to 4 disjoint classes",
              disjointClasses(o).size() == 4);

        // Partition property: the pieces concatenate to the alphabet and
        // never share a character.
        const QStringList parts = disjointClasses(o);
        QSet<QChar> all;
        int total = 0;
        bool noOverlap = true;
        for (const QString &p : parts) {
            total += p.size();
            for (const QChar c : p) {
                if (all.contains(c)) noOverlap = false;
                all.insert(c);
            }
        }
        check("classes partition the alphabet (no character in two classes)",
              noOverlap && total == alphabet(o).size());

        Options dup = o;
        dup.extra = QStringLiteral("abc!");   // all already covered
        check("extra characters already in a class add nothing",
              alphabet(dup).size() == 89 && disjointClasses(dup).size() == 4);

        Options novel = o;
        novel.extra = QStringLiteral("€✓");  // euro sign, check mark
        check("genuinely new extra characters form a fifth class",
              alphabet(novel).size() == 91 && disjointClasses(novel).size() == 5,
              QString::number(alphabet(novel).size()));

        Options dirty = o;
        dirty.extra = QStringLiteral("  \t§§ \n");
        check("extra strips whitespace and de-duplicates",
              alphabet(dirty).size() == 90, QString::number(alphabet(dirty).size()));

        Options none;
        none.classes = 0;
        check("no classes and no extra yields an empty alphabet",
              alphabet(none).isEmpty() && disjointClasses(none).isEmpty());

        // v0.1.128 review: invisible characters used to reach the alphabet.
        Options invisible = o;
        invisible.classes = Lower;
        invisible.extra = QString(QChar(0x200B))      // zero-width space
                        + QChar(0x00AD)               // soft hyphen
                        + QChar(0xFEFF);              // BOM / ZWNBSP
        check("invisible formatting characters never enter the alphabet",
              alphabet(invisible).size() == 26, QString::number(alphabet(invisible).size()));

        // A supplementary-plane character arrives as two UTF-16 surrogates.
        // Drawing them independently would emit unpaired halves.
        Options astral = o;
        astral.classes = Lower;
        astral.extra = QString::fromUcs4(U"\U0001F600\U0001D538");  // emoji + double-struck A
        check("non-BMP characters are dropped rather than split into surrogates",
              alphabet(astral).size() == 26, QString::number(alphabet(astral).size()));
        bool noSurrogate = true;
        for (const QChar c : alphabet(astral)) if (c.isSurrogate()) noSurrogate = false;
        check("the alphabet never contains a lone surrogate", noSurrogate);

        Options lookalikeExtra = o;
        lookalikeExtra.classes = Lower;
        lookalikeExtra.excludeLookalikes = true;
        lookalikeExtra.extra = QStringLiteral("0O1lI");
        check("look-alike filter applies to extra characters too",
              alphabet(lookalikeExtra).size() == 25,
              alphabet(lookalikeExtra));
    }

    // ── entropy accounting ─────────────────────────────────────────
    std::printf("\n— entropy ────────────────────────────────────────\n");
    {
        Options o;
        o.requireEachClass = false;
        const double expect = 20.0 * std::log2(89.0);
        check("unconstrained entropy is length * log2(alphabet)",
              nearly(entropyBits(o), expect),
              QString::number(entropyBits(o)));

        Options c = o;
        c.requireEachClass = true;
        check("requiring one of each REDUCES entropy",
              entropyBits(c) < entropyBits(o) && entropyBits(c) > expect - 1.0,
              QString("%1 vs %2").arg(entropyBits(c)).arg(entropyBits(o)));

        Options single;
        single.classes = Lower;
        single.requireEachClass = true;
        check("a single class makes the constraint vacuous (no entropy loss)",
              nearly(entropyBits(single), 20.0 * std::log2(26.0)),
              QString::number(entropyBits(single)));
        check("acceptance is exactly 1 for a single class",
              nearly(acceptanceProbability(single), 1.0));

        // Closed form for length == number of classes: every position is
        // a different class, so k! * product(|Ci|) of 89^4 strings qualify.
        Options four = o;
        four.length = 4;
        four.requireEachClass = true;
        const double want = 24.0 * 26 * 26 * 10 * 27 / std::pow(89.0, 4);
        check("acceptance matches the closed form at length == classes",
              nearly(acceptanceProbability(four), want, 1e-9),
              QString("%1 vs %2").arg(acceptanceProbability(four)).arg(want));

        Options phrase;
        phrase.mode = Mode::Passphrase;
        phrase.words = 5;
        check("passphrase entropy is words * log2(wordlist)",
              nearly(entropyBits(phrase), 5.0 * std::log2(double(wordlistSize()))),
              QString::number(entropyBits(phrase)));
        Options phraseDigits = phrase;
        phraseDigits.appendDigits = true;
        check("two appended digits add log2(100) bits",
              nearly(entropyBits(phraseDigits) - entropyBits(phrase), std::log2(100.0)));

        Options empty;
        empty.classes = 0;
        check("an empty alphabet has zero entropy", nearly(entropyBits(empty), 0.0));

        check("strength bands are ordered",
              strength(10).score == 0 && strength(50).score == 1 &&
              strength(70).score == 2 && strength(100).score == 3 &&
              strength(200).score == 4);
        check("default settings land in the top band",
              strength(entropyBits(Options())).score == 4);
    }

    // ── validation ─────────────────────────────────────────────────
    std::printf("\n— validate ───────────────────────────────────────\n");
    {
        Options o;
        check("defaults validate", validate(o).isEmpty(), validate(o));

        Options shortOpt = o; shortOpt.length = kMinLength - 1;
        check("length below the floor is rejected", !validate(shortOpt).isEmpty());
        Options longOpt = o;  longOpt.length = kMaxLength + 1;
        check("length above the ceiling is rejected", !validate(longOpt).isEmpty());

        Options noClass = o; noClass.classes = 0;
        check("no character set is rejected", !validate(noClass).isEmpty());

        Options oneChar; oneChar.classes = 0; oneChar.extra = QStringLiteral("x");
        check("a one-character alphabet is rejected", !validate(oneChar).isEmpty());

        Options tooShortForClasses = o;
        tooShortForClasses.length = 4;
        tooShortForClasses.extra = QStringLiteral("€");  // 5th class
        check("length 4 cannot hold one of five sets",
              !validate(tooShortForClasses).isEmpty(),
              validate(tooShortForClasses));

        Options fewWords; fewWords.mode = Mode::Passphrase; fewWords.words = kMinWords - 1;
        check("too few words is rejected", !validate(fewWords).isEmpty());
        Options manyWords; manyWords.mode = Mode::Passphrase; manyWords.words = kMaxWords + 1;
        check("too many words is rejected", !validate(manyWords).isEmpty());

        // The message has to name a control the user can actually see.
        Options crowded = o;
        crowded.length = 4;
        crowded.extra = QStringLiteral("\u20AC");
        check("the length message quotes the real checkbox label",
              validate(tooShortForClasses).contains(QStringLiteral("sets in use")) ||
              validate(tooShortForClasses).contains(QStringLiteral("At least one from each set")),
              validate(tooShortForClasses));

        check("generate() returns empty for invalid options",
              generate(noClass).isEmpty() && generate(shortOpt).isEmpty());
        check("generateMany() returns empty for invalid options",
              generateMany(noClass, 5).isEmpty());
    }

    // ── generation ─────────────────────────────────────────────────
    std::printf("\n— generate ───────────────────────────────────────\n");
    {
        Options o;   // 20 chars, four classes, one-of-each required
        const QString alpha = alphabet(o);
        const QStringList parts = disjointClasses(o);

        bool rightLength = true, inAlphabet = true, complete = true;
        QSet<QString> distinct;
        for (int i = 0; i < 400; ++i) {
            const QString p = generate(o);
            if (p.size() != o.length) rightLength = false;
            for (const QChar c : p) if (!alpha.contains(c)) inAlphabet = false;
            for (const QString &part : parts) {
                bool found = false;
                for (const QChar c : p) if (part.contains(c)) { found = true; break; }
                if (!found) complete = false;
            }
            distinct.insert(p);
        }
        check("every password has the requested length", rightLength);
        check("every character comes from the alphabet", inAlphabet);
        check("every password contains one of each selected set", complete);
        check("400 passwords are all distinct", distinct.size() == 400,
              QString::number(distinct.size()));

        // The constraint must actually be doing work: at length 4 only
        // ~7% of naive draws contain all four classes, so with the flag
        // off a large sample is overwhelmingly likely to miss some.
        Options loose = o;
        loose.length = 4;
        loose.requireEachClass = false;
        bool sawIncomplete = false;
        for (int i = 0; i < 500 && !sawIncomplete; ++i) {
            const QString p = generate(loose);
            for (const QString &part : parts) {
                bool found = false;
                for (const QChar c : p) if (part.contains(c)) { found = true; break; }
                if (!found) { sawIncomplete = true; break; }
            }
        }
        check("with the flag off, short passwords do miss sets", sawIncomplete);

        Options noLook = o;
        noLook.excludeLookalikes = true;
        bool clean = true;
        for (int i = 0; i < 300; ++i) {
            const QString p = generate(noLook);
            for (const QChar c : QStringLiteral("0O1lI")) if (p.contains(c)) clean = false;
        }
        check("excluding look-alikes never emits 0 O 1 l I", clean);

        // Character frequency, constraint off so the draw is exactly
        // uniform. 89 symbols x 600 expected each; sd ~= 24.4, so a
        // 6-sigma envelope is +/- 147.
        Options flat = o;
        flat.requireEachClass = false;
        flat.length = 89;
        QHash<QChar, int> freq;
        for (int i = 0; i < 600; ++i)
            for (const QChar c : generate(flat)) freq[c]++;
        int worst = 0;
        for (const QChar c : alpha) worst = qMax(worst, qAbs(freq.value(c) - 600));
        check("character frequencies are uniform within 6 sigma",
              worst < 147, QString::number(worst));

        check("generateMany honours the count", generateMany(o, 7).size() == 7);
        check("generateMany clamps above the ceiling",
              generateMany(o, kMaxCount + 50).size() == kMaxCount);
        check("generateMany clamps below one", generateMany(o, 0).size() == 1);
    }

    // ── passphrases ────────────────────────────────────────────────
    std::printf("\n— passphrase ─────────────────────────────────────\n");
    {
        Options p;
        p.mode = Mode::Passphrase;
        p.words = 5;

        bool shape = true;
        for (int i = 0; i < 200; ++i) {
            const QStringList w = generate(p).split(QLatin1Char('-'));
            if (w.size() != 5) { shape = false; break; }
            for (const QString &one : w)
                if (one.size() < 4 || one.size() > 6 || one != one.toLower()) shape = false;
        }
        check("five hyphenated lowercase words", shape);

        Options caps = p; caps.capitalise = true;
        const QStringList cw = generate(caps).split(QLatin1Char('-'));
        bool capped = cw.size() == 5;
        for (const QString &one : cw) if (one.isEmpty() || !one.at(0).isUpper()) capped = false;
        check("capitalise raises the first letter of every word", capped);

        Options digits = p; digits.appendDigits = true;
        const QStringList dw = generate(digits).split(QLatin1Char('-'));
        check("appending digits adds one two-digit group",
              dw.size() == 6 && dw.last().size() == 2 &&
              dw.last().at(0).isDigit() && dw.last().at(1).isDigit(),
              generate(digits));

        Options none = p; none.separator = QChar();
        check("the 'none' separator concatenates",
              !generate(none).contains(QLatin1Char('-')));

        Options space = p; space.separator = QLatin1Char(' ');
        check("a space separator produces five words",
              generate(space).split(QLatin1Char(' ')).size() == 5);

        QSet<QString> distinct;
        for (int i = 0; i < 300; ++i) distinct.insert(generate(p));
        check("300 passphrases are all distinct", distinct.size() == 300,
              QString::number(distinct.size()));
    }

    // ── options equality ───────────────────────────────────────────
    std::printf("\n— options ────────────────────────────────────────\n");
    {
        Options a, b;
        check("default options compare equal", a == b);
        b.length = 21;
        check("a changed field compares unequal", !(a == b));
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
