// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Diagram tab's "Import Mermaid" dialog, driven for real (offscreen).
//
// Reported from v0.1.134: importing a sequenceDiagram replaced the user's
// diagram with an empty one carrying a "# (Mermaid import found no nodes or
// edges)" comment. Now the dialog stays open and shows why (the type and what
// is supported), and the diagram is left untouched. A supported flowchart
// still imports. No QMessageBox is involved (they crash offscreen on
// Windows); $HOME and the config dirs are isolated on every OS.

#include "diagram/diagram_editor.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>

#include <cstdio>

static int g_pass = 0, g_fail = 0;
static void check(const char *what, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", what); ++g_pass; }
    else    { std::printf("  [FAIL] %s%s%s\n", what, detail.isEmpty() ? "" : " — ",
                          detail.toUtf8().constData()); ++g_fail; }
    std::fflush(stdout);
}

static QPushButton *buttonWithText(QDialog *dlg, const QString &text) {
    for (QPushButton *b : dlg->findChildren<QPushButton *>())
        if (b->text().remove(QLatin1Char('&')) == text) return b;
    return nullptr;
}

struct Outcome {
    bool sawDialog = false;
    bool errorShown = false;
    QString errorText;
    bool stillOpenAfterImport = false;
};

// Click "Import Mermaid", paste `mermaid`, press Import, record whether the
// dialog stayed open and what its error label says, then Cancel if still open.
static Outcome runImport(DiagramEditor &de, const QString &mermaid) {
    Outcome o;
    QToolButton *btn = nullptr;
    for (QToolButton *b : de.findChildren<QToolButton *>())
        if (b->text() == QLatin1String("Import Mermaid")) btn = b;
    if (!btn) return o;

    bool done = false;
    QTimer driver;
    driver.setInterval(20);
    QObject::connect(&driver, &QTimer::timeout, [&] {
        auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dlg || done) return;
        done = true;
        o.sawDialog = true;
        if (auto *edit = dlg->findChild<QPlainTextEdit *>()) edit->setPlainText(mermaid);
        if (QPushButton *imp = buttonWithText(dlg, QStringLiteral("Import"))) imp->click();
        QApplication::processEvents();
        o.stillOpenAfterImport = dlg->isVisible();
        if (auto *err = dlg->findChild<QLabel *>(QStringLiteral("mermaidImportError"))) {
            o.errorShown = !err->isHidden();
            o.errorText = err->text();
        }
        if (dlg->isVisible()) dlg->reject();   // Cancel: leave the diagram alone
    });
    driver.start();
    // Watchdog: never let a stuck modal hang the suite.
    QTimer::singleShot(10000, [&] {
        if (auto *dlg = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dlg->reject();
    });
    btn->click();   // runs the dialog's exec(); returns when it closes
    driver.stop();
    return o;
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
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QStandardPaths::setTestModeEnabled(true);
    QApplication app(argc, argv);
    std::printf("=== Import Mermaid dialog ===\n");

    DiagramEditor de;
    de.resize(900, 600);
    de.show();
    const QString original = QStringLiteral("diagram flow\nnode keep [Keep me]\nkeep -> other\n");
    de.setNpdText(original);

    // 1. Unsupported type: error in the dialog, diagram untouched.
    Outcome o = runImport(de, QStringLiteral("sequenceDiagram\n  Alice->>Bob: Hi\n"));
    check("the Import Mermaid dialog opened", o.sawDialog);
    check("Import on a sequenceDiagram keeps the dialog open", o.stillOpenAfterImport);
    check("the dialog shows why", o.errorShown && o.errorText.startsWith(
              QStringLiteral("Mermaid \"sequenceDiagram\" diagrams cannot be imported. Supported: ")),
          o.errorText);
    check("the diagram is NOT replaced by an empty one", de.npdText() == original, de.npdText());

    // 2. Nothing to import: same treatment.
    o = runImport(de, QStringLiteral("flowchart TD\n"));
    check("an empty flowchart keeps the dialog open with a reason",
          o.stillOpenAfterImport && o.errorShown &&
              o.errorText == QLatin1String("the Mermaid flowchart has no nodes or edges to import"),
          o.errorText);
    check("the diagram is still untouched", de.npdText() == original, de.npdText());

    // 3. A supported flowchart imports and closes the dialog.
    o = runImport(de, QStringLiteral("flowchart LR\n  A[Start] --> B{OK?}\n"));
    check("a flowchart import closes the dialog", o.sawDialog && !o.stillOpenAfterImport);
    check("the flowchart replaced the diagram",
          de.npdText().contains(QLatin1String("node A [Start]")) &&
              de.npdText().contains(QLatin1String("A -> B")),
          de.npdText());

    std::printf("\n=== Summary: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
