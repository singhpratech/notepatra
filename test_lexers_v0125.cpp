// Regression tests for v0.1.26's new lexer subclasses + extension map.
// Verifies that:
//   1. The 6 new lexers (Rust, Go, Swift, TypeScript, Kotlin, PowerShell)
//      report correct language() and have non-empty keyword sets.
//   2. The extension map routes .ps1, .rs, .go, .swift, .ts, .kt to the
//      correct language strings (i.e. no longer to Batch / C++ / Java /
//      JavaScript / Java).
//   3. createLexerForLanguage() returns a non-null QsciLexer instance
//      for every new language string.
//
// Pure C++/Qt -- no GUI, no widgets, runs in ~10 ms.

#include "src/lexerutils.h"
#include "src/lexer_go.h"
#include "src/lexer_kotlin.h"
#include "src/lexer_powershell.h"
#include "src/lexer_rust.h"
#include "src/lexer_swift.h"
#include "src/lexer_typescript.h"

#include <Qsci/qscilexer.h>
#include <QApplication>
#include <QString>
#include <QStringList>
#include <QRegExp>
#include <cstdio>
#include <cstdlib>

static int passed = 0;
static int failed = 0;

static void check(const char *label, bool cond) {
    if (cond) {
        ++passed;
        std::printf("  [PASS] %s\n", label);
    } else {
        ++failed;
        std::printf("  [FAIL] %s\n", label);
    }
}

static void check_eq(const char *label, const QString &got, const QString &want) {
    const bool ok = (got == want);
    if (ok) {
        ++passed;
        std::printf("  [PASS] %s\n", label);
    } else {
        ++failed;
        std::printf("  [FAIL] %s -- got '%s', want '%s'\n",
                    label, got.toUtf8().constData(), want.toUtf8().constData());
    }
}

static QString contains(const char *cstr) { return QString::fromUtf8(cstr); }

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // ───── 1. Lexer subclasses report correct language() and keywords ─────
    {
        LexerRust lex;
        check_eq("Rust language()",       lex.language(), QStringLiteral("Rust"));
        const QString kw = QString::fromUtf8(lex.keywords(1) ? lex.keywords(1) : "");
        check("Rust keywords contain 'fn'",     kw.contains("fn"));
        check("Rust keywords contain 'let'",    kw.contains("let"));
        check("Rust keywords contain 'mut'",    kw.contains("mut"));
        check("Rust keywords contain 'impl'",   kw.contains("impl"));
        check("Rust keywords contain 'pub'",    kw.contains("pub"));
        check("Rust keywords contain 'crate'",  kw.contains("crate"));
        check("Rust keywords contain 'match'",  kw.contains("match"));
        check("Rust keywords contain 'async'",  kw.contains("async"));
        check("Rust set 2 has primitive types", QString::fromUtf8(lex.keywords(2)).contains("i32"));
        check("Rust set 3 returns nullptr",     lex.keywords(3) == nullptr);
    }
    {
        LexerGo lex;
        check_eq("Go language()", lex.language(), QStringLiteral("Go"));
        const QString kw = QString::fromUtf8(lex.keywords(1));
        check("Go keywords contain 'func'",     kw.contains("func"));
        check("Go keywords contain 'package'",  kw.contains("package"));
        check("Go keywords contain 'defer'",    kw.contains("defer"));
        check("Go keywords contain 'go'",       kw.contains(QRegExp("\\bgo\\b")));
        check("Go keywords contain 'chan'",     kw.contains("chan"));
        check("Go keywords contain 'interface'", kw.contains("interface"));
        check("Go set 2 has 'true'",            QString::fromUtf8(lex.keywords(2)).contains("true"));
        check("Go set 2 has 'nil'",             QString::fromUtf8(lex.keywords(2)).contains("nil"));
    }
    {
        LexerSwift lex;
        check_eq("Swift language()", lex.language(), QStringLiteral("Swift"));
        const QString kw = QString::fromUtf8(lex.keywords(1));
        check("Swift keywords contain 'func'",       kw.contains("func"));
        check("Swift keywords contain 'protocol'",   kw.contains("protocol"));
        check("Swift keywords contain 'extension'",  kw.contains("extension"));
        check("Swift keywords contain 'guard'",      kw.contains("guard"));
        check("Swift keywords contain 'actor'",      kw.contains("actor"));
        check("Swift set 2 has 'String'",            QString::fromUtf8(lex.keywords(2)).contains("String"));
    }
    {
        LexerTypeScript lex;
        check_eq("TypeScript language()", lex.language(), QStringLiteral("TypeScript"));
        const QString kw = QString::fromUtf8(lex.keywords(1));
        check("TS keywords contain 'interface'", kw.contains("interface"));
        check("TS keywords contain 'type'",      kw.contains(QRegExp("\\btype\\b")));
        check("TS keywords contain 'readonly'",  kw.contains("readonly"));
        check("TS keywords contain 'keyof'",     kw.contains("keyof"));
        check("TS keywords contain 'never'",     kw.contains("never"));
        check("TS keywords contain 'satisfies'", kw.contains("satisfies"));
    }
    {
        LexerKotlin lex;
        check_eq("Kotlin language()", lex.language(), QStringLiteral("Kotlin"));
        const QString kw = QString::fromUtf8(lex.keywords(1));
        check("Kotlin keywords contain 'fun'",      kw.contains(QRegExp("\\bfun\\b")));
        check("Kotlin keywords contain 'val'",      kw.contains(QRegExp("\\bval\\b")));
        check("Kotlin keywords contain 'when'",     kw.contains(QRegExp("\\bwhen\\b")));
        check("Kotlin keywords contain 'data'",     kw.contains(QRegExp("\\bdata\\b")));
        check("Kotlin keywords contain 'sealed'",   kw.contains("sealed"));
        check("Kotlin keywords contain 'suspend'",  kw.contains("suspend"));
    }
    {
        LexerPowerShell lex;
        check_eq("PowerShell language()", lex.language(), QStringLiteral("PowerShell"));
        check_eq("PowerShell scintilla lexer name", QString::fromUtf8(lex.lexer()), QStringLiteral("powershell"));
        // v0.1.32 — keyword set ordering corrected to match Scintilla's
        // SCLEX_POWERSHELL contract: QScintilla 1-based set N maps to
        // Scintilla idx N-1. Old (broken) ordering put cmdlet verbs in the
        // Aliases slot and full Verb-Noun names in User1 — `New-Object` and
        // friends rendered as default text on Windows. New layout:
        //   set 1 → idx 0 PowerShell Keywords  (function, if, foreach, …)
        //   set 2 → idx 1 Cmdlets              (full Verb-Noun + verbs)
        //   set 3 → idx 2 Aliases              (ls, cd, gci, …)
        //   set 5 → idx 4 User1                (.NET type names)
        const QString kw1 = QString::fromUtf8(lex.keywords(1));
        check("PS set1 has 'function'",  kw1.contains("function"));
        check("PS set1 has 'foreach'",   kw1.contains("foreach"));
        check("PS set1 has 'param'",     kw1.contains("param"));
        const QString kw2 = QString::fromUtf8(lex.keywords(2));
        check("PS set2 has 'New-Object' (full cmdlet)",   kw2.contains("New-Object"));
        check("PS set2 has 'Get-Item' (full cmdlet)",     kw2.contains("Get-Item"));
        check("PS set2 has 'Where-Object' (full cmdlet)", kw2.contains("Where-Object"));
        check("PS set2 has 'Get' (cmdlet verb)",          kw2.contains(QRegExp("\\bGet\\b")));
        check("PS set2 has 'Set' (cmdlet verb)",          kw2.contains(QRegExp("\\bSet\\b")));
        check("PS set2 has 'New' (cmdlet verb)",          kw2.contains(QRegExp("\\bNew\\b")));
        const QString kw3 = QString::fromUtf8(lex.keywords(3));
        check("PS set3 has 'ls' (alias)", kw3.contains(QRegExp("\\bls\\b")));
        check("PS set3 has 'cd' (alias)", kw3.contains(QRegExp("\\bcd\\b")));
        check("PS set3 has 'gci' (alias)", kw3.contains(QRegExp("\\bgci\\b")));
        check("PS set4 returns nullptr (Functions slot intentionally empty)",
              lex.keywords(4) == nullptr);
        const QString kw5 = QString::fromUtf8(lex.keywords(5));
        check("PS set5 has 'string' (.NET type)", kw5.contains(QRegExp("\\bstring\\b")));
        check("PS set5 has 'datetime' (.NET type)", kw5.contains("datetime"));
        check("PS description for keyword style",
              lex.description(8).toLower().contains("keyword"));
        check("PS description for cmdlet style",
              lex.description(9).toLower().contains("cmdlet"));
    }

    // ───── 2. Extension map routes new languages correctly ─────────────
    check_eq(".ps1   -> PowerShell",  detectLanguageFromPath("script.ps1", ""), QStringLiteral("PowerShell"));
    check_eq(".psm1  -> PowerShell",  detectLanguageFromPath("module.psm1", ""), QStringLiteral("PowerShell"));
    check_eq(".psd1  -> PowerShell",  detectLanguageFromPath("manifest.psd1", ""), QStringLiteral("PowerShell"));
    check_eq(".rs    -> Rust",        detectLanguageFromPath("main.rs", ""), QStringLiteral("Rust"));
    check_eq(".go    -> Go",          detectLanguageFromPath("server.go", ""), QStringLiteral("Go"));
    check_eq(".swift -> Swift",       detectLanguageFromPath("AppDelegate.swift", ""), QStringLiteral("Swift"));
    check_eq(".kt    -> Kotlin",      detectLanguageFromPath("Main.kt", ""), QStringLiteral("Kotlin"));
    check_eq(".kts   -> Kotlin",      detectLanguageFromPath("build.kts", ""), QStringLiteral("Kotlin"));
    check_eq(".ts    -> TypeScript",  detectLanguageFromPath("index.ts", ""), QStringLiteral("TypeScript"));
    check_eq(".tsx   -> TypeScript",  detectLanguageFromPath("App.tsx", ""), QStringLiteral("TypeScript"));
    check_eq(".bat   -> Batch",       detectLanguageFromPath("install.bat", ""), QStringLiteral("Batch"));
    check_eq(".cmd   -> Batch (NOT PowerShell)", detectLanguageFromPath("setup.cmd", ""), QStringLiteral("Batch"));

    // Verify Phase 4 additions — v0.1.55 promoted these from
    // closest-fit fallbacks to dedicated lexers (lexer_extras.cpp).
    check_eq(".dart  -> Dart",        detectLanguageFromPath("main.dart", ""), QStringLiteral("Dart"));
    check_eq(".zig   -> Zig",         detectLanguageFromPath("main.zig", ""), QStringLiteral("Zig"));
    check_eq(".jl    -> Julia",       detectLanguageFromPath("primes.jl", ""), QStringLiteral("Julia"));
    check_eq(".clj   -> Lua",         detectLanguageFromPath("core.clj", ""), QStringLiteral("Lua"));  // still fallback (Lisp lexer pending)
    check_eq(".ex    -> Elixir",      detectLanguageFromPath("server.ex", ""), QStringLiteral("Elixir"));

    // ───── 3. createLexerForLanguage returns instances for new langs ───
    {
        QsciLexer *l = createLexerForLanguage("PowerShell", nullptr);
        check("createLexerForLanguage(PowerShell) is non-null", l != nullptr);
        if (l) check("PS instance is LexerPowerShell", dynamic_cast<LexerPowerShell*>(l) != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("Rust", nullptr);
        check("createLexerForLanguage(Rust) is non-null",       l != nullptr);
        if (l) check("Rust instance is LexerRust", dynamic_cast<LexerRust*>(l) != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("Go", nullptr);
        check("createLexerForLanguage(Go) is non-null",         l != nullptr);
        if (l) check("Go instance is LexerGo", dynamic_cast<LexerGo*>(l) != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("Swift", nullptr);
        check("createLexerForLanguage(Swift) is non-null",      l != nullptr);
        if (l) check("Swift instance is LexerSwift", dynamic_cast<LexerSwift*>(l) != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("Kotlin", nullptr);
        check("createLexerForLanguage(Kotlin) is non-null",     l != nullptr);
        if (l) check("Kotlin instance is LexerKotlin", dynamic_cast<LexerKotlin*>(l) != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("TypeScript", nullptr);
        check("createLexerForLanguage(TypeScript) is non-null", l != nullptr);
        if (l) check("TS instance is LexerTypeScript", dynamic_cast<LexerTypeScript*>(l) != nullptr);
        delete l;
    }

    // ───── 4. Sanity: existing lexers still work ───────────────────────
    {
        QsciLexer *l = createLexerForLanguage("Python", nullptr);
        check("createLexerForLanguage(Python) is non-null", l != nullptr);
        delete l;
    }
    {
        QsciLexer *l = createLexerForLanguage("Plain Text", nullptr);
        check("createLexerForLanguage(Plain Text) is null", l == nullptr);
        delete l;
    }

    std::printf("\n=== Summary: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
