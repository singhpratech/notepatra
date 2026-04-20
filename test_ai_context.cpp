/**
 * Unit tests for AiContext::buildWorkspaceContextBlock — the function that
 * assembles the cross-file awareness block sent to the local AI model.
 *
 * Runs headless (no QApplication, no GUI). Link only against aipanel.cpp's
 * object file plus the anon-namespace support code it needs.
 */
#include "src/ai_context.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>
#include <cstdio>

static int passed = 0, failed = 0;

static void check(const char *name, bool ok, const QString &detail = {}) {
    if (ok) { std::printf("  [PASS] %s\n", name); ++passed; }
    else    {
        std::printf("  [FAIL] %s%s%s\n", name,
                    detail.isEmpty() ? "" : " — ",
                    detail.toUtf8().constData());
        ++failed;
    }
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    std::printf("=== AiContext::buildWorkspaceContextBlock ===\n\n");

    // 1. Empty input → empty block (no header spam when there's nothing
    //    meaningful to report — matters for greenfield chat with no files).
    {
        QString out = AiContext::buildWorkspaceContextBlock({}, {}, {}, {});
        check("empty → empty", out.isEmpty());
    }

    // 2. Single file → current-file section emitted, no "Other open files".
    {
        AiContext::OpenTabInfo cur;
        cur.filePath = "/home/x/src/main.py";
        cur.displayName = "main.py";
        cur.language = "Python";
        cur.text = "print('hello')\n";
        cur.isCurrent = true;
        QVector<AiContext::OpenTabInfo> tabs{cur};
        QString out = AiContext::buildWorkspaceContextBlock(
            cur.filePath, cur.text, tabs, "/home/x/src");

        check("single file — header present", out.contains("# Workspace context"));
        check("single file — root reported", out.contains("/home/x/src"));
        check("single file — current marker", out.contains("← current"));
        check("single file — content embedded", out.contains("print('hello')"));
        check("single file — no 'Other open files'", !out.contains("## Other open files"));
    }

    // 3. Three tabs → each listed, each excerpt present, current flagged once.
    {
        auto mk = [](const QString &p, const QString &lang, const QString &t, bool c) {
            AiContext::OpenTabInfo i;
            i.filePath = p; i.displayName = p.section('/', -1);
            i.language = lang; i.text = t; i.isCurrent = c;
            return i;
        };
        QVector<AiContext::OpenTabInfo> tabs = {
            mk("/ws/main.py",      "Python",   "def main(): pass\n", true),
            mk("/ws/utils.py",     "Python",   "def helper(): return 42\n", false),
            mk("/ws/README.md",    "Markdown", "# Project\n\nReadme body.\n", false),
        };
        QString out = AiContext::buildWorkspaceContextBlock(
            tabs[0].filePath, tabs[0].text, tabs, "/ws");

        check("3 tabs — current content in body",  out.contains("def main(): pass"));
        check("3 tabs — sibling content excerpt",  out.contains("def helper(): return 42"));
        check("3 tabs — readme excerpt",           out.contains("# Project"));
        check("3 tabs — tab list summary",         out.contains("Open editor tabs (3)"));
        check("3 tabs — each path listed",
              out.contains("/ws/main.py") && out.contains("/ws/utils.py")
              && out.contains("/ws/README.md"));

        int currentMarkers = out.count(QStringLiteral("← current"));
        check("3 tabs — exactly one current marker",
              currentMarkers == 1,
              QStringLiteral("got %1").arg(currentMarkers));

        check("3 tabs — has 'Other open files' section",
              out.contains("## Other open files"));
    }

    // 4. Huge current file → truncation token appears, body is bounded.
    {
        QString huge;
        huge.reserve(60000);
        for (int i = 0; i < 3000; ++i) huge += "line " + QString::number(i) + "\n";

        QVector<AiContext::OpenTabInfo> tabs;
        AiContext::OpenTabInfo cur;
        cur.filePath = "/ws/big.py"; cur.language = "Python";
        cur.text = huge; cur.isCurrent = true;
        tabs.append(cur);

        QString out = AiContext::buildWorkspaceContextBlock(
            cur.filePath, huge, tabs, "/ws");

        check("huge — truncation notice present", out.contains("truncated"));
        // Current file section is capped at 12 000 chars; total output
        // should stay well under 20 000 even with headers.
        check("huge — total output bounded (<25 KB)",
              out.size() < 25000,
              QStringLiteral("got %1").arg(out.size()));
        check("huge — still contains early content",
              out.contains("line 0\n"));
        check("huge — does NOT contain last line",
              !out.contains("line 2999\n"));
    }

    // 5. Many other tabs → combined excerpt budget respected; at most
    //    ~10 KB of "other file" body emitted.
    {
        QVector<AiContext::OpenTabInfo> tabs;
        AiContext::OpenTabInfo cur;
        cur.filePath = "/ws/main.py"; cur.language = "Python";
        cur.text = "main\n"; cur.isCurrent = true;
        tabs.append(cur);
        for (int i = 0; i < 20; ++i) {
            AiContext::OpenTabInfo other;
            other.filePath = QString("/ws/sib_%1.py").arg(i);
            other.displayName = QString("sib_%1.py").arg(i);
            other.language = "Python";
            other.text = QString(3000, QChar('a'));  // 3 KB each
            other.isCurrent = false;
            tabs.append(other);
        }
        QString out = AiContext::buildWorkspaceContextBlock(
            cur.filePath, cur.text, tabs, "/ws");

        check("20 tabs — 'omitted' notice appears",
              out.contains("additional open files omitted"));
        check("20 tabs — total output under 40 KB",
              out.size() < 40000,
              QStringLiteral("got %1").arg(out.size()));
    }

    // 6. Unsaved buffer (no filePath) → displayName used, no crash.
    {
        AiContext::OpenTabInfo cur;
        cur.filePath = "";
        cur.displayName = "new 1";
        cur.language = "Plain Text";
        cur.text = "scratch notes\n";
        cur.isCurrent = true;
        QVector<AiContext::OpenTabInfo> tabs{cur};
        QString out = AiContext::buildWorkspaceContextBlock("", cur.text, tabs, "");

        check("unsaved — displayName fallback", out.contains("new 1"));
        check("unsaved — body embedded", out.contains("scratch notes"));
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
