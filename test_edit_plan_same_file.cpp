// SPDX-License-Identifier: GPL-3.0-or-later
//
// test_edit_plan_same_file — in Compose mode every dry-run proposal is built
// against the file on disk, so two edits to the same file arrive as two
// (before, after) pairs with the SAME before. EditPlanList used to queue them
// as two rows; Apply wrote them in order and the second silently overwrote
// the first. Both edits must land.
//
// The host side is simulated exactly as AIPanel::applyComposerEdits does it:
// write each emitted (path, after) pair in order, last write wins.

#include "src/edit_plan.h"

#include <QApplication>
#include <QList>
#include <QMap>
#include <QPair>
#include <QPushButton>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>

static int g_pass = 0, g_fail = 0;

static void check(const char *label, bool ok) {
    if (ok) { ++g_pass; std::printf("  [PASS] %s\n", label); }
    else    { ++g_fail; std::printf("  [FAIL] %s\n", label); }
}

// Click Apply All and return the file contents the host would end up with.
static QMap<QString, QString> applyAll(EditPlanList &plan) {
    QMap<QString, QString> disk;
    QObject::connect(&plan, &EditPlanList::applyRequested, &plan,
                     [&disk](const QList<QPair<QString, QString>> &edits) {
                         for (const auto &e : edits) disk[e.first] = e.second;
                     });
    for (QPushButton *b : plan.findChildren<QPushButton *>())
        if (b->text() == QStringLiteral("Apply All")) b->click();
    QObject::disconnect(&plan, &EditPlanList::applyRequested, nullptr, nullptr);
    return disk;
}

int main(int argc, char **argv) {
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    qputenv("XDG_DATA_HOME", home.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("APPDATA", home.path().toUtf8());
    qputenv("USERPROFILE", home.path().toUtf8());
#endif
    QApplication app(argc, argv);
    std::printf("=== test_edit_plan_same_file ===\n");

    const QString path = QStringLiteral("/ws/src/main.cpp");
    const QString base = QStringLiteral(
        "#include <cstdio>\n"
        "int helper() {\n"
        "    return 1;\n"
        "}\n"
        "\n"
        "int main() {\n"
        "    return helper();\n"
        "}\n");
    // Edit 1 touches helper(); edit 2 touches main(). Both against `base`.
    QString edit1 = base;
    edit1.replace(QStringLiteral("    return 1;\n"), QStringLiteral("    return 42;\n"));
    QString edit2 = base;
    edit2.replace(QStringLiteral("    return helper();\n"),
                  QStringLiteral("    std::puts(\"hi\");\n    return helper();\n"));

    {
        EditPlanList plan;
        plan.setWorkspaceRoot(QStringLiteral("/ws"));
        plan.addEdit(path, base, edit1);
        plan.addEdit(path, base, edit2);
        plan.addEdit(QStringLiteral("/ws/other.txt"), QStringLiteral("a\n"),
                     QStringLiteral("b\n"));
        const QMap<QString, QString> disk = applyAll(plan);
        const QString final_ = disk.value(path);
        check("two edits to one file: first edit landed", final_.contains("return 42;"));
        check("two edits to one file: second edit landed", final_.contains("std::puts"));
        check("two edits to one file: rest of the file intact",
              final_.startsWith("#include <cstdio>\n") && final_.endsWith("}\n")
                  && final_.count(QStringLiteral("int main()")) == 1);
        check("other file unaffected", disk.value("/ws/other.txt") == "b\n");
        check("one row per file", plan.count() == 2);
    }

    // CRLF content: endings survive the merge.
    {
        QString crlf = base;
        crlf.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
        QString a = crlf, b = crlf;
        a.replace(QStringLiteral("return 1;"), QStringLiteral("return 7;"));
        b.replace(QStringLiteral("int main()"), QStringLiteral("int main(void)"));
        EditPlanList plan;
        plan.addEdit(path, crlf, a);
        plan.addEdit(path, crlf, b);
        const QString final_ = applyAll(plan).value(path);
        check("CRLF: both edits landed",
              final_.contains("return 7;\r\n") && final_.contains("int main(void) {\r\n"));
        check("CRLF: no bare LF introduced", !final_.contains(QRegularExpression("[^\r]\n")));
    }

    // A proposal already built on top of the pending edit replaces it.
    {
        EditPlanList plan;
        plan.addEdit(path, base, edit1);
        QString chained = edit1;
        chained.replace(QStringLiteral("return 42;"), QStringLiteral("return 43;"));
        plan.addEdit(path, edit1, chained);
        check("chained proposal: single row", plan.count() == 1);
        check("chained proposal: latest content applied", applyAll(plan).value(path) == chained);
    }

    // An already-applied row is never rewritten by a later proposal.
    {
        EditPlanList plan;
        plan.addEdit(path, base, edit1);
        plan.markApplied({path});
        plan.addEdit(path, edit1, edit1 + "// tail\n");
        check("after apply: new proposal gets its own row", plan.count() == 2);
        check("after apply: only the new proposal is re-applied",
              applyAll(plan).value(path) == edit1 + "// tail\n");
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
