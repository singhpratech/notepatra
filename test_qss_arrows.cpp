/**
 * Source lint: a Qt stylesheet must not silently delete a widget's arrows.
 *
 * Two ways to lose them, both shipped at some point in this codebase:
 *
 *   1. Any QSS rule matching a QSpinBox hands its up/down buttons to the
 *      stylesheet engine, which draws nothing unless ::up-button and
 *      ::down-button supply images. sqlfmtpanel's Indent spinner shipped
 *      with no arrows this way.
 *   2. Styling QComboBox::drop-down without a ::down-arrow image removes
 *      the arrow entirely. restclient's method combo shipped with none.
 *
 * The CSS border-triangle idiom is NOT a fix — Qt renders `width: 0` plus
 * transparent side borders as a filled box, and it would not look native
 * on Windows or macOS anyway. Theme these widgets by QPalette instead.
 *
 * This lints the real sources, so a panel added later cannot quietly
 * reintroduce either shape.
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

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

// Only look inside string literals — a comment mentioning QSpinBox is fine.
static QStringList literalsIn(const QString &src) {
    QStringList out;
    static const QRegularExpression re(QStringLiteral("\"((?:[^\"\\\\]|\\\\.)*)\""));
    auto it = re.globalMatch(src);
    while (it.hasNext()) out << it.next().captured(1);
    return out;
}

int main() {
    std::printf("=== QSS arrow lint ===\n\n");

    QDir srcDir(QStringLiteral(NOTEPATRA_SOURCE_DIR "/src"));
    QStringList files;
    for (const QString &f : srcDir.entryList({ QStringLiteral("*.cpp") }, QDir::Files))
        files << srcDir.filePath(f);
    check("found the source tree to lint", files.size() > 20,
          QString::number(files.size()));

    QStringList spinOffenders, dropOffenders, triangleOffenders;
    int literalsScanned = 0;

    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString src = QString::fromUtf8(f.readAll());
        const QString base = QFileInfo(path).fileName();

        for (const QString &lit : literalsIn(src)) {
            ++literalsScanned;

            // (1) a QSS selector naming QSpinBox / QAbstractSpinBox
            static const QRegularExpression spinSel(
                QStringLiteral("\\b(QSpinBox|QDoubleSpinBox|QAbstractSpinBox)\\b\\s*[,{:]"));
            if (spinSel.match(lit).hasMatch() &&
                !lit.contains(QStringLiteral("::up-button")) &&
                !lit.contains(QStringLiteral("::up-arrow"))) {
                if (!spinOffenders.contains(base)) spinOffenders << base;
            }

            // (2) ::drop-down styled with no ::down-arrow image alongside it
            if (lit.contains(QStringLiteral("::drop-down")) &&
                !lit.contains(QStringLiteral("::down-arrow"))) {
                if (!dropOffenders.contains(base)) dropOffenders << base;
            }

            // (3) the border-triangle idiom, which Qt draws as a filled box
            if ((lit.contains(QStringLiteral("::up-arrow")) ||
                 lit.contains(QStringLiteral("::down-arrow"))) &&
                lit.contains(QStringLiteral("solid transparent"))) {
                if (!triangleOffenders.contains(base)) triangleOffenders << base;
            }
        }
    }

    // Vacuity guard: if no literals were scanned the lint proves nothing.
    check("the lint actually scanned stylesheet literals", literalsScanned > 500,
          QString::number(literalsScanned));

    check("no stylesheet targets a spin box without styling its buttons",
          spinOffenders.isEmpty(), spinOffenders.join(QStringLiteral(", ")));
    check("no stylesheet styles ::drop-down without a ::down-arrow image",
          dropOffenders.isEmpty(), dropOffenders.join(QStringLiteral(", ")));
    check("no stylesheet uses the CSS border-triangle idiom for an arrow",
          triangleOffenders.isEmpty(), triangleOffenders.join(QStringLiteral(", ")));

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
