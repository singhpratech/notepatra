#include "lexerutils.h"

#include <QFileInfo>
#include <QHash>
#include <QStringList>

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerpascal.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>

// Notepatra-local lexer subclasses that fill the gaps QScintilla doesn't
// ship lexers for (PowerShell, Rust, Go, Swift, TypeScript, Kotlin).
#include "lexer_go.h"
#include "lexer_kotlin.h"
#include "lexer_powershell.h"
#include "lexer_rust.h"
#include "lexer_swift.h"
#include "lexer_typescript.h"
#include "lexer_extras.h"

// v0.1.84 — Plain-text and CSV lexers (no-op syntax / column-aware) shipped
// alongside the curated keyword tables. Headers are provided by parallel
// agents; the class names follow the existing global-namespace pattern of
// LexerDart / LexerToml / etc.
#include "lexer_plaintext.h"
#include "lexer_csv.h"

// v0.1.84 — curated SCI_SETKEYWORDS strings for every supported language.
// Pushed into the editor in populateExtraKeywords() right after setLexer().
#include "sql_keywords.h"
#include "lang_keywords.h"

#if __has_include(<Qsci/qscilexeravs.h>)
#include <Qsci/qscilexeravs.h>
#define HAS_LEXER_AVS
#endif
#if __has_include(<Qsci/qscilexercoffeescript.h>)
#include <Qsci/qscilexercoffeescript.h>
#define HAS_LEXER_COFFEESCRIPT
#endif
#if __has_include(<Qsci/qscilexerd.h>)
#include <Qsci/qscilexerd.h>
#define HAS_LEXER_D
#endif
#if __has_include(<Qsci/qscilexerfortran.h>)
#include <Qsci/qscilexerfortran.h>
#include <Qsci/qscilexerfortran77.h>
#define HAS_LEXER_FORTRAN
#endif
#if __has_include(<Qsci/qscilexeridl.h>)
#include <Qsci/qscilexeridl.h>
#define HAS_LEXER_IDL
#endif
#if __has_include(<Qsci/qscilexermatlab.h>)
#include <Qsci/qscilexermatlab.h>
#include <Qsci/qscilexeroctave.h>
#define HAS_LEXER_MATLAB
#endif
#if __has_include(<Qsci/qscilexerpo.h>)
#include <Qsci/qscilexerpo.h>
#define HAS_LEXER_PO
#endif
#if __has_include(<Qsci/qscilexerpostscript.h>)
#include <Qsci/qscilexerpostscript.h>
#define HAS_LEXER_POSTSCRIPT
#endif
#if __has_include(<Qsci/qscilexerpov.h>)
#include <Qsci/qscilexerpov.h>
#define HAS_LEXER_POV
#endif
#if __has_include(<Qsci/qscilexerproperties.h>)
#include <Qsci/qscilexerproperties.h>
#define HAS_LEXER_PROPERTIES
#endif
#if __has_include(<Qsci/qscilexerspice.h>)
#include <Qsci/qscilexerspice.h>
#define HAS_LEXER_SPICE
#endif
#if __has_include(<Qsci/qscilexertcl.h>)
#include <Qsci/qscilexertcl.h>
#define HAS_LEXER_TCL
#endif
#if __has_include(<Qsci/qscilexertex.h>)
#include <Qsci/qscilexertex.h>
#define HAS_LEXER_TEX
#endif
#if __has_include(<Qsci/qscilexerverilog.h>)
#include <Qsci/qscilexerverilog.h>
#define HAS_LEXER_VERILOG
#endif
#if __has_include(<Qsci/qscilexervhdl.h>)
#include <Qsci/qscilexervhdl.h>
#define HAS_LEXER_VHDL
#endif
#if __has_include(<Qsci/qscilexermasm.h>)
#include <Qsci/qscilexermasm.h>
#define HAS_LEXER_MASM
#endif
#if __has_include(<Qsci/qscilexernasm.h>)
#include <Qsci/qscilexernasm.h>
#define HAS_LEXER_NASM
#endif
#if __has_include(<Qsci/qscilexerintelhex.h>)
#include <Qsci/qscilexerintelhex.h>
#define HAS_LEXER_INTELHEX
#endif
#if __has_include(<Qsci/qscilexersrec.h>)
#include <Qsci/qscilexersrec.h>
#define HAS_LEXER_SREC
#endif

QString buildSaveAsFilters(const QString &currentLanguage,
                           QString *selectedFilter) {
    // Curated language → glob-list table for the Save As dropdown. Separate
    // from the extension-detection extMap in detectLanguageFromPath because
    // save-time grouping is by intent ("save as Python") not by detection
    // disambiguation. Ordered roughly by popularity then alphabetically.
    //
    // Adding a language: append below + (recommended) add a matching entry
    // to extMap so reading the file back picks the right lexer.
    static const struct { const char *name; const char *exts; } entries[] = {
        {"Plain Text",          "*.txt *.log *.out *.text"},
        {"Markdown",            "*.md *.markdown *.mkd *.rmd *.rst"},
        {"Python",              "*.py *.pyw *.pyx *.pyi *.pxd"},
        {"JavaScript",          "*.js *.mjs *.cjs *.jsx"},
        {"TypeScript",          "*.ts *.tsx *.mts *.cts"},
        {"C",                   "*.c *.h"},
        {"C++",                 "*.cpp *.cxx *.cc *.hpp *.hxx *.hh *.ino"},
        {"C#",                  "*.cs"},
        {"Java",                "*.java"},
        {"Rust",                "*.rs"},
        {"Go",                  "*.go"},
        {"Ruby",                "*.rb *.rake *.gemspec *.rbw"},
        {"PHP",                 "*.php *.phtml"},
        {"Swift",               "*.swift"},
        {"Kotlin",              "*.kt *.kts *.ktm"},
        {"Scala",               "*.scala *.sc"},
        {"Dart",                "*.dart"},
        {"Solidity",            "*.sol"},
        {"Zig",                 "*.zig *.zon"},
        {"Julia",               "*.jl"},
        {"R",                   "*.r *.R"},
        {"F#",                  "*.fs *.fsx *.fsi"},
        {"HTML",                "*.html *.htm *.xhtml *.html5 *.vue *.svelte"},
        {"CSS",                 "*.css *.scss *.sass *.less"},
        {"XML",                 "*.xml *.svg *.xsl *.xsd *.plist *.rss *.xaml"},
        {"JSON",                "*.json *.jsonc *.geojson *.webmanifest *.har"},
        {"JSON Lines",          "*.jsonl *.ndjson"},
        {"JSON5",               "*.json5"},
        {"YAML",                "*.yaml *.yml"},
        {"TOML",                "*.toml"},
        {"CSV / TSV",           "*.csv *.tsv"},
        {"SQL",                 "*.sql *.ddl *.dml *.pgsql *.plsql *.tsql *.mysql *.sqlite"},
        {"Bash / Shell",        "*.sh *.bash *.zsh *.fish *.ksh *.csh"},
        {"PowerShell",          "*.ps1 *.psm1 *.psd1"},
        {"Batch",               "*.bat *.cmd"},
        {"Perl",                "*.pl *.pm *.pod *.t"},
        {"Lua",                 "*.lua *.luau *.wlua"},
        {"Fortran",             "*.f *.f90 *.f95 *.f03 *.for *.fpp *.f77"},
        {"MATLAB / Octave",     "*.m *.mat *.oct"},
        {"Pascal",              "*.pas *.pp *.dpr *.dpk"},
        {"Verilog",             "*.v *.sv *.svh"},
        {"VHDL",                "*.vhd *.vhdl"},
        {"LaTeX",               "*.tex *.latex *.bib *.sty"},
        {"Protobuf",            "*.proto"},
        {"GraphQL",             "*.graphql *.gql"},
        {"HCL / Terraform",     "*.tf *.tfvars *.hcl"},
        {"Thrift",              "*.thrift"},
        {"GDScript",            "*.gd *.tres *.tscn"},
        {"Nim",                 "*.nim *.nims"},
        {"Cython",              "*.pyx *.pxd"},
        {"Mojo",                "*.mojo"},
        {"Crystal",             "*.cr"},
        {"Elixir",              "*.ex *.exs"},
        {"Groovy",              "*.groovy *.gradle"},
        {"Apex",                "*.cls *.trigger"},
        {"Vala",                "*.vala *.vapi"},
        {"Hack",                "*.hack"},
        {"D",                   "*.d *.di"},
        {"Jinja",               "*.jinja *.jinja2 *.j2"},
        {"Liquid",              "*.liquid"},
        {"Twig",                "*.twig"},
        {"Dockerfile",          "Dockerfile Containerfile *.dockerfile"},
        {"Fish",                "*.fish"},
        {"Nushell",             "*.nu"},
        {"CMake",               "CMakeLists.txt *.cmake"},
        {"Makefile",            "Makefile makefile GNUmakefile"},
        {"Diff / Patch",        "*.diff *.patch"},
        {"Properties / INI",    "*.ini *.cfg *.conf *.properties *.env *.editorconfig"},
        {"TeX BibTeX",          "*.bib *.bibtex"},
        {"PostScript",          "*.ps *.eps"},
        {"Assembly",            "*.asm *.s *.S *.nasm *.masm"},
        {"Intel HEX",           "*.hex *.ihex"},
        {"Motorola S-Record",   "*.srec *.s19 *.s28"},
        {"Gitignore",           ".gitignore .dockerignore"},
    };

    QStringList filters;
    filters.reserve(static_cast<int>(sizeof(entries) / sizeof(entries[0])) + 1);
    filters << QStringLiteral("All Files (*)");

    QString preselect;
    const QString cur = currentLanguage.trimmed();
    for (const auto &e : entries) {
        const QString name = QString::fromLatin1(e.name);
        const QString filter = QStringLiteral("%1 (%2)")
                                   .arg(name, QString::fromLatin1(e.exts));
        filters << filter;
        // Try exact match first, then case-insensitive fallback.
        if (!cur.isEmpty() && preselect.isEmpty() &&
            (cur.compare(name, Qt::CaseInsensitive) == 0 ||
             (cur == QLatin1String("CSV") && name == QLatin1String("CSV / TSV")) ||
             (cur == QLatin1String("Bash") && name == QLatin1String("Bash / Shell")))) {
            preselect = filter;
        }
    }

    if (selectedFilter) *selectedFilter = preselect;
    return filters.join(QStringLiteral(";;"));
}

// v0.1.87 follow-up — extract first "*.ext" pattern from a filter entry.
// "Python (*.py *.pyw *.pyx)"        → "py"
// "Dockerfile (Dockerfile *.docker)" → "" (first pattern is bare filename)
// "All Files (*)"                    → ""
// "CMake (CMakeLists.txt *.cmake)"   → ""  (first pattern is bare filename)
// Returns the extension WITHOUT the leading dot to match
// QFileDialog::setDefaultSuffix's API.
QString firstExtensionFromFilter(const QString &filter) {
    const int openParen = filter.indexOf(QLatin1Char('('));
    const int closeParen = filter.lastIndexOf(QLatin1Char(')'));
    if (openParen < 0 || closeParen <= openParen + 1) return {};
    const QString patterns = filter.mid(openParen + 1, closeParen - openParen - 1);
    // Walk patterns left-to-right; first "*.ext" wins (the curated table puts
    // the canonical extension first — e.g. "*.py" before "*.pyw").
    for (const QString &pRaw :
         patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString p = pRaw.trimmed();
        if (p == QLatin1String("*")) return {};       // All Files (*)
        if (p.startsWith(QLatin1String("*."))) {
            return p.mid(2);                          // "py", not ".py"
        }
        // First pattern is a bare filename (Dockerfile / CMakeLists.txt /
        // .gitignore) — auto-suffixing makes no sense, bail.
        return {};
    }
    return {};
}

// v0.1.87 follow-up — post-Accept safety net for QFileDialog::setDefaultSuffix.
// Some platform file dialogs (notably the Linux GTK theme on certain distros,
// and the macOS native dialog when the suffix changes mid-session) silently
// drop setDefaultSuffix. This walks the filter's "*.ext" patterns and ensures
// the returned path ends with at least one of them, appending the first if
// not. Filter-only with bare filenames OR the "All Files" filter return path
// unchanged.
QString applySaveAsFilterSuffix(const QString &path, const QString &filter) {
    if (path.isEmpty()) return path;
    const int openParen = filter.indexOf(QLatin1Char('('));
    const int closeParen = filter.lastIndexOf(QLatin1Char(')'));
    if (openParen < 0 || closeParen <= openParen + 1) return path;
    const QString patterns = filter.mid(openParen + 1, closeParen - openParen - 1);

    QStringList exts;
    bool sawBareName = false;
    for (const QString &pRaw :
         patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString p = pRaw.trimmed();
        if (p == QLatin1String("*")) return path;          // All Files (*)
        if (p.startsWith(QLatin1String("*."))) {
            exts << QStringLiteral(".") + p.mid(2);        // ".py"
        } else {
            sawBareName = true;
        }
    }
    if (exts.isEmpty()) return path;                       // Dockerfile-only

    // If the filename matches one of the bare-name patterns exactly, accept.
    if (sawBareName) {
        const QString basename = QFileInfo(path).fileName();
        for (const QString &pRaw :
             patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            const QString p = pRaw.trimmed();
            if (!p.startsWith(QLatin1String("*.")) && p != QLatin1String("*") &&
                p.compare(basename, Qt::CaseInsensitive) == 0) {
                return path;
            }
        }
    }

    // Does path already end with one of the filter's extensions?
    for (const QString &ext : exts) {
        if (path.endsWith(ext, Qt::CaseInsensitive)) return path;
    }

    return path + exts.first();
}

QString detectLanguageFromPath(const QString &path, const QString &text) {
    const QFileInfo fi(path);
    const QString ext = fi.suffix().toLower();
    const QString name = fi.fileName();

    static const QHash<QString, QString> extMap = {
        {"py", "Python"}, {"pyw", "Python"}, {"pyx", "Python"}, {"pyi", "Python"},
        {"pxd", "Python"}, {"ipynb", "JSON"}, {"sage", "Python"}, {"bzl", "Python"},
        {"js", "JavaScript"}, {"mjs", "JavaScript"}, {"cjs", "JavaScript"},
        {"jsx", "JavaScript"},
        {"ts", "TypeScript"}, {"tsx", "TypeScript"}, {"mts", "TypeScript"}, {"cts", "TypeScript"},
        {"coffee", "CoffeeScript"}, {"litcoffee", "CoffeeScript"},
        {"c", "C"}, {"h", "C"},
        {"cpp", "C++"}, {"cxx", "C++"}, {"cc", "C++"}, {"hpp", "C++"},
        {"hxx", "C++"}, {"hh", "C++"}, {"ino", "C++"},
        {"m", "Octave"}, {"mm", "C++"},
        {"cs", "C#"},
        {"java", "Java"},
        {"scala", "Java"}, {"sc", "Java"}, {"groovy", "Java"}, {"gradle", "Java"},
        {"d", "D"}, {"di", "D"},
        {"rs", "Rust"},
        {"go", "Go"},
        {"swift", "Swift"},
        {"kt", "Kotlin"}, {"kts", "Kotlin"}, {"ktm", "Kotlin"},
        {"html", "HTML"}, {"htm", "HTML"}, {"xhtml", "HTML"}, {"html5", "HTML"},
        {"vue", "HTML"}, {"svelte", "HTML"}, {"jsp", "HTML"},
        {"erb", "HTML"}, {"ejs", "HTML"}, {"hbs", "HTML"},
        {"twig", "HTML"}, {"jinja", "HTML"}, {"jinja2", "HTML"},
        {"php", "HTML"}, {"phtml", "HTML"},
        {"css", "CSS"}, {"scss", "CSS"}, {"sass", "CSS"}, {"less", "CSS"},
        {"xml", "XML"}, {"svg", "XML"}, {"xsl", "XML"}, {"xsd", "XML"},
        {"plist", "XML"}, {"rss", "XML"}, {"atom", "XML"}, {"wsdl", "XML"},
        {"xaml", "XML"}, {"csproj", "XML"}, {"vcxproj", "XML"}, {"sln", "XML"},
        {"json", "JSON"}, {"jsonc", "JSON"}, {"geojson", "JSON"},
        {"webmanifest", "JSON"}, {"har", "JSON"},
        // v0.1.55 — JSON Lines (each line = one JSON object) and
        // newline-delimited JSON. Same lexer as JSON; QScintilla's JSON
        // tokeniser handles per-line records correctly.
        {"jsonl", "JSON"}, {"ndjson", "JSON"},
        {"sql", "SQL"}, {"ddl", "SQL"}, {"dml", "SQL"},
        {"pgsql", "SQL"}, {"plsql", "SQL"}, {"tsql", "SQL"},
        {"mysql", "SQL"}, {"sqlite", "SQL"}, {"hql", "SQL"},
        {"cql", "SQL"}, {"psql", "SQL"},
        {"sh", "Bash"}, {"bash", "Bash"}, {"zsh", "Bash"}, {"fish", "Bash"},
        {"ksh", "Bash"}, {"csh", "Bash"}, {"tcsh", "Bash"},
        {"bat", "Batch"}, {"cmd", "Batch"},
        {"ps1", "PowerShell"}, {"psm1", "PowerShell"}, {"psd1", "PowerShell"},
        {"ps1xml", "XML"},
        {"rb", "Ruby"}, {"rake", "Ruby"}, {"gemspec", "Ruby"}, {"rbw", "Ruby"},
        {"pl", "Perl"}, {"pm", "Perl"}, {"pod", "Perl"}, {"t", "Perl"},
        {"lua", "Lua"}, {"luau", "Lua"}, {"wlua", "Lua"},
        {"tcl", "TCL"}, {"tk", "TCL"},
        {"f", "Fortran"}, {"f90", "Fortran"}, {"f95", "Fortran"},
        {"f03", "Fortran"}, {"for", "Fortran"}, {"fpp", "Fortran"},
        {"f77", "Fortran77"},
        {"mat", "Matlab"}, {"oct", "Octave"},
        {"asm", "ASM"}, {"s", "ASM"}, {"S", "ASM"},
        {"nasm", "NASM"}, {"masm", "MASM"},
        {"v", "Verilog"}, {"sv", "Verilog"}, {"svh", "Verilog"},
        {"vhd", "VHDL"}, {"vhdl", "VHDL"},
        {"tex", "TeX"}, {"latex", "TeX"}, {"bib", "BibTeX"}, {"sty", "TeX"},
        // .cls is ambiguous: LaTeX class file OR Salesforce Apex.
        // Apex .cls files are far more common in dev workflows; route to
        // Apex by default. Users who hit a LaTeX .cls can pick TeX from
        // the Language menu.
        {"bibtex", "BibTeX"},
        {"ps", "PostScript"}, {"eps", "PostScript"},
        {"idl", "IDL"}, {"pro", "IDL"},
        {"ini", "Properties"}, {"cfg", "Properties"}, {"conf", "Properties"},
        {"properties", "Properties"}, {"env", "Properties"},
        {"editorconfig", "Properties"}, {"gitconfig", "Properties"},
        {"pov", "POV"}, {"inc", "POV"},
        {"spice", "Spice"}, {"cir", "Spice"},
        {"md", "Markdown"}, {"markdown", "Markdown"}, {"mkd", "Markdown"},
        {"rst", "Markdown"},
        {"yml", "YAML"}, {"yaml", "YAML"},
        {"toml", "Properties"},
        {"diff", "Diff"}, {"patch", "Diff"},
        {"pas", "Pascal"}, {"pp", "Pascal"}, {"dpr", "Pascal"}, {"dpk", "Pascal"},
        {"cmake", "CMake"},
        {"avs", "AVS"}, {"avsi", "AVS"},
        {"r", "R"}, {"rmd", "Markdown"},
        {"hex", "IntelHex"}, {"ihex", "IntelHex"},
        {"srec", "SRecord"}, {"s19", "SRecord"}, {"s28", "SRecord"},
        {"log", "Plain Text"}, {"out", "Plain Text"}, {"txt", "Plain Text"},
        // v0.1.84 — dedicated column-aware CSV lexer (was Plain Text fallback)
        {"csv", "CSV"}, {"tsv", "CSV"},
        {"dockerignore", "Gitignore"}, {"gitignore", "Gitignore"},
        // v0.1.55 — dedicated lexers for ~31 languages that previously
        // fell back to closest-fit. Each is a small QsciLexer* subclass
        // with curated keyword sets. See src/lexer_extras.cpp.
        {"dart", "Dart"},
        {"sol", "Solidity"},
        {"zig", "Zig"}, {"zon", "Zig"},
        {"vala", "Vala"}, {"vapi", "Vala"},
        {"hack", "Hack"}, {"hh", "Hack"},
        {"jl", "Julia"},
        {"proto", "Protobuf"},
        {"fs", "F#"}, {"fsx", "F#"}, {"fsi", "F#"},
        {"tf", "HCL"}, {"tfvars", "HCL"}, {"hcl", "HCL"},
        {"thrift", "Thrift"},
        {"graphql", "GraphQL"}, {"gql", "GraphQL"},
        {"gd", "GDScript"}, {"tres", "GDScript"}, {"tscn", "GDScript"},
        {"nim", "Nim"}, {"nims", "Nim"},
        {"pyx", "Cython"}, {"pxd", "Cython"},  // override Python default
        {"mojo", "Mojo"}, {"🔥", "Mojo"},
        {"cr", "Crystal"},
        {"ex", "Elixir"}, {"exs", "Elixir"},
        {"scala", "Scala"}, {"sc", "Scala"},
        {"groovy", "Groovy"}, {"gradle", "Groovy"},
        {"cls", "Apex"}, {"trigger", "Apex"},
        {"jinja", "Jinja"}, {"jinja2", "Jinja"}, {"j2", "Jinja"},
        {"liquid", "Liquid"},
        {"twig", "Twig"},
        {"dockerfile", "Dockerfile"},
        {"fish", "Fish"},
        {"nu", "Nushell"},
        {"toml", "TOML"},
        {"json5", "JSON5"},
        // ── still-fallback for languages whose lexer needs more work ──
        {"erl", "Perl"}, {"hrl", "Perl"},   // Erlang
        {"hs", "Bash"}, {"lhs", "Bash"},    // Haskell — needs custom QsciLexerCustom
        {"ml", "Bash"}, {"mli", "Bash"},    // OCaml — needs custom (* *) comment lexer
        {"clj", "Lua"}, {"cljs", "Lua"}, {"cljc", "Lua"}, {"edn", "Lua"},   // Clojure
        {"elm", "Bash"},                    // Elm
        {"v", "Verilog"},                   // V (lang) collision with Verilog
        {"vlang", "C++"},                   // V language unique extension
        {"odin", "C++"},                    // Odin — C-family
        {"sb", "Bash"}, {"sbn", "Bash"},    // SmallBASIC / scratch
        {"awk", "Bash"},                    // AWK — Bash lexer is close enough
    };

    static const QHash<QString, QString> nameMap = {
        {"Makefile", "Makefile"}, {"makefile", "Makefile"}, {"GNUmakefile", "Makefile"},
        {"Dockerfile", "Dockerfile"}, {"Containerfile", "Dockerfile"},
        {"Vagrantfile", "Ruby"}, {"Rakefile", "Ruby"},
        {"Gemfile", "Ruby"}, {"Podfile", "Ruby"},
        {"CMakeLists.txt", "CMake"}, {"meson.build", "Python"},
        {".bashrc", "Bash"}, {".bash_profile", "Bash"}, {".zshrc", "Bash"},
        {".profile", "Bash"}, {".gitignore", "Gitignore"}, {".dockerignore", "Gitignore"},
        {".editorconfig", "Properties"}, {".env", "DotEnv"},
        {"Cargo.toml", "TOML"}, {"Cargo.lock", "TOML"}, {"pyproject.toml", "TOML"},
        {"poetry.lock", "TOML"}, {"uv.lock", "TOML"}, {"Pipfile", "TOML"},
        {"package.json", "JSON"}, {"tsconfig.json", "JSON"},
        {"composer.json", "JSON"}, {".eslintrc", "JSON"},
        {"requirements.txt", "Plain Text"}, {"go.mod", "Bash"}, {"go.sum", "Plain Text"},
    };

    QString lang = nameMap.value(name, extMap.value(ext, "Plain Text"));

    if (lang == "JSON") {
        const QString trimmed = text.trimmed();
        const bool startsBrace = trimmed.startsWith('{') || trimmed.startsWith('[');
        const bool hasQuoted = trimmed.left(200).contains('"');
        if (!startsBrace || !hasQuoted) lang = "JavaScript";
    }

    return lang;
}

QsciLexer *createLexerForLanguage(const QString &lang, QObject *parent) {
    // v0.1.84 — explicit Plain Text and CSV lexers (previously the function
    // returned nullptr for "Plain Text" and let the caller paint default
    // styles). The new lexers give us consistent paper/font and a CSV-aware
    // column-coloured renderer for delimited files.
    if (lang == "Plain Text") return new LexerPlainText(parent);
    if (lang == "CSV")        return new LexerCsv(parent);
    if (lang == "Python") return new QsciLexerPython(parent);
    if (lang == "JavaScript") return new QsciLexerJavaScript(parent);
#ifdef HAS_LEXER_COFFEESCRIPT
    if (lang == "CoffeeScript") return new QsciLexerCoffeeScript(parent);
#endif
    if (lang == "C" || lang == "C++") return new QsciLexerCPP(parent);
    if (lang == "C#") return new QsciLexerCSharp(parent);
#ifdef HAS_LEXER_D
    if (lang == "D") return new QsciLexerD(parent);
#endif
    if (lang == "Java") return new QsciLexerJava(parent);
    if (lang == "HTML" || lang == "PHP") return new QsciLexerHTML(parent);
    if (lang == "CSS") return new QsciLexerCSS(parent);
    if (lang == "XML") return new QsciLexerXML(parent);
    if (lang == "JSON") return new QsciLexerJSON(parent);
    if (lang == "SQL") return new QsciLexerSQL(parent);
    if (lang == "Bash") return new QsciLexerBash(parent);
    if (lang == "Batch") return new QsciLexerBatch(parent);
    if (lang == "Ruby") return new QsciLexerRuby(parent);
    if (lang == "Perl") return new QsciLexerPerl(parent);
    if (lang == "Lua") return new QsciLexerLua(parent);
#ifdef HAS_LEXER_TCL
    if (lang == "TCL") return new QsciLexerTCL(parent);
#endif
#ifdef HAS_LEXER_FORTRAN
    if (lang == "Fortran") return new QsciLexerFortran(parent);
    if (lang == "Fortran77") return new QsciLexerFortran77(parent);
#endif
#ifdef HAS_LEXER_MATLAB
    if (lang == "Matlab") return new QsciLexerMatlab(parent);
    if (lang == "Octave") return new QsciLexerOctave(parent);
#endif
#ifdef HAS_LEXER_IDL
    if (lang == "IDL") return new QsciLexerIDL(parent);
#endif
#ifdef HAS_LEXER_NASM
    if (lang == "ASM" || lang == "NASM") return new QsciLexerNASM(parent);
#endif
#ifdef HAS_LEXER_MASM
    if (lang == "MASM") return new QsciLexerMASM(parent);
#endif
#ifdef HAS_LEXER_VERILOG
    if (lang == "Verilog") return new QsciLexerVerilog(parent);
#endif
#ifdef HAS_LEXER_VHDL
    if (lang == "VHDL") return new QsciLexerVHDL(parent);
#endif
#ifdef HAS_LEXER_TEX
    if (lang == "TeX") return new QsciLexerTeX(parent);
#endif
#ifdef HAS_LEXER_POSTSCRIPT
    if (lang == "PostScript") return new QsciLexerPostScript(parent);
#endif
#ifdef HAS_LEXER_POV
    if (lang == "POV") return new QsciLexerPOV(parent);
#endif
#ifdef HAS_LEXER_SPICE
    if (lang == "Spice") return new QsciLexerSpice(parent);
#endif
#ifdef HAS_LEXER_AVS
    if (lang == "AVS") return new QsciLexerAVS(parent);
#endif
#ifdef HAS_LEXER_PROPERTIES
    if (lang == "Properties") return new QsciLexerProperties(parent);
#endif
#ifdef HAS_LEXER_PO
    if (lang == "PO") return new QsciLexerPO(parent);
#endif
#ifdef HAS_LEXER_INTELHEX
    if (lang == "IntelHex") return new QsciLexerIntelHex(parent);
#endif
#ifdef HAS_LEXER_SREC
    if (lang == "SRecord") return new QsciLexerSRec(parent);
#endif
    if (lang == "Markdown") return new QsciLexerMarkdown(parent);
    if (lang == "YAML") return new QsciLexerYAML(parent);
    if (lang == "Diff") return new QsciLexerDiff(parent);
    if (lang == "Pascal") return new QsciLexerPascal(parent);
    if (lang == "CMake") return new QsciLexerCMake(parent);
    if (lang == "Makefile") return new QsciLexerMakefile(parent);

    // Notepatra-local lexer subclasses for languages QScintilla doesn't
    // ship lexers for. Each fills a real-world UX gap:
    //   - PowerShell: was being highlighted as Batch (cmd.exe). Wrong.
    //   - Rust / Go / Swift: were being highlighted as C++. Keywords
    //     like `fn`, `let`, `func`, `package`, `protocol` rendered
    //     as plain identifiers.
    //   - Kotlin: was being highlighted as Java. `fun`, `val`, `var`,
    //     `data class`, `when`, `sealed` rendered as identifiers.
    //   - TypeScript: was being highlighted as JavaScript. `interface`,
    //     `type`, `as`, `is`, `readonly`, `keyof` rendered as identifiers.
    if (lang == "PowerShell") return new LexerPowerShell(parent);
    if (lang == "Rust")       return new LexerRust(parent);
    if (lang == "Go")         return new LexerGo(parent);
    if (lang == "Swift")      return new LexerSwift(parent);
    if (lang == "Kotlin")     return new LexerKotlin(parent);
    if (lang == "TypeScript") return new LexerTypeScript(parent);

    // v0.1.55 — bulk lexer additions targeting 80+ supported languages.
    // Each is a small QsciLexer* subclass overriding keywords() only;
    // the base lexer handles tokenisation. See src/lexer_extras.cpp for
    // the curated keyword tables (sourced from each language's official
    // reference docs). Adding a new language here is additive: existing
    // callers continue to fall through to nullptr → "Plain Text" lexer.
    if (lang == "Dart")       return new LexerDart(parent);
    if (lang == "Solidity")   return new LexerSolidity(parent);
    if (lang == "Zig")        return new LexerZig(parent);
    if (lang == "Vala")       return new LexerVala(parent);
    if (lang == "Hack")       return new LexerHack(parent);
    if (lang == "Julia")      return new LexerJulia(parent);
    if (lang == "R")          return new LexerR(parent);
    if (lang == "Protobuf")   return new LexerProtobuf(parent);
    if (lang == "F#")         return new LexerFSharp(parent);
    if (lang == "HCL" || lang == "Terraform") return new LexerHCL(parent);
    if (lang == "Thrift")     return new LexerThrift(parent);
    if (lang == "GraphQL")    return new LexerGraphQL(parent);
    if (lang == "GDScript")   return new LexerGDScript(parent);
    if (lang == "Nim")        return new LexerNim(parent);
    if (lang == "Cython")     return new LexerCython(parent);
    if (lang == "Mojo")       return new LexerMojo(parent);
    if (lang == "Crystal")    return new LexerCrystal(parent);
    if (lang == "Elixir")     return new LexerElixir(parent);
    if (lang == "Scala")      return new LexerScala(parent);
    if (lang == "Groovy")     return new LexerGroovy(parent);
    if (lang == "Apex")       return new LexerApex(parent);
    if (lang == "Jinja")      return new LexerJinja(parent);
    if (lang == "Liquid")     return new LexerLiquid(parent);
    if (lang == "Twig")       return new LexerTwig(parent);
    if (lang == "Dockerfile") return new LexerDockerfile(parent);
    if (lang == "Fish")       return new LexerFish(parent);
    if (lang == "Nushell")    return new LexerNushell(parent);
    if (lang == "TOML")       return new LexerToml(parent);
    if (lang == "DotEnv")     return new LexerEnv(parent);
    if (lang == "Gitignore")  return new LexerGitignore(parent);
    if (lang == "JSON5")      return new LexerJson5(parent);
    if (lang == "BibTeX")     return new LexerBibTeX(parent);

    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────
// populateExtraKeywords — v0.1.84 palette overhaul
//
// Pushes the curated keyword strings from sql_keywords.h / lang_keywords.h
// into the live editor via SCI_SETKEYWORDS. The QScintilla lexers ship
// small built-in keyword sets that lag modern language additions; this
// fills the gaps without subclassing every lexer.
//
// Slot semantics differ per lexer (verified against the QScintilla
// keywords(int set) headers under /usr/include/.../Qsci/):
//   QsciLexerSQL    : 0=reserved, 1=extra (functions), 4=user-defined 1 (types)
//   QsciLexerPython : 0=primary, 1=highlighted identifiers (builtins)
//   QsciLexerJavaScript / QsciLexerCPP : 0=primary, 1=secondary (types)
//   QsciLexerRuby   : 0=primary
//   QsciLexerHTML   : 0=HTML tags, 1=JavaScript, 2=VBScript, 3=Python,
//                     4=PHP, 5=SGML
//   QsciLexerCSS    : 0=CSS1 properties, 1=pseudo-classes, 2=CSS2/3 props
//   QsciLexerBash   : 0=primary
//   QsciLexerLua    : 0=primary, 1=basic functions, 2..7=specialised libs
//   QsciLexerYAML   : 0=boolean-ish words
//   QsciLexerJSON   : not keyword-driven; skip
//
// The first arg is cast to uintptr_t (not unsigned long) to match the
// SendScintilla(uint, uintptr_t, const char*) overload — same trick as
// sqlfmtpanel.cpp:399.
// ─────────────────────────────────────────────────────────────────────────
void populateExtraKeywords(QsciScintilla *editor, const QString &lang) {
    if (!editor) return;
    auto send = [editor](int slot, const char *s) {
        if (!s) return;
        editor->SendScintilla(QsciScintilla::SCI_SETKEYWORDS,
                              (uintptr_t)slot, s);
    };

    if (lang == "SQL") {
        send(0, notepatra::kSqlReserved);
        send(1, notepatra::kSqlFunctions);
        send(4, notepatra::kSqlTypes);
    }
    else if (lang == "Python") {
        send(0, notepatra::langkw::kPythonKW);
        // QsciLexerPython slot 1 = "Highlighted identifiers" — perfect for
        // builtins (print, len, ...). Typing names route to slot 1 too as
        // a single space-separated concat would dilute styling — keep them
        // routed via the future "user-defined" hook instead.
        send(1, notepatra::langkw::kPythonBuiltins);
    }
    else if (lang == "JavaScript") {
        send(0, notepatra::langkw::kJavaScriptKW);
        send(1, notepatra::langkw::kJavaScriptBuiltins);
    }
    else if (lang == "TypeScript") {
        // TS uses our LexerTypeScript subclass which inherits QsciLexerCPP.
        // CPP lexer: slot 0 = primary keywords, slot 1 = secondary keywords.
        // Concatenate JS + TS extras in slot 0 isn't possible at runtime
        // without an allocation owned by the editor — instead push TS extras
        // into slot 0 and types into slot 1. (LexerTypeScript already merges
        // JS+TS in its keywords() override; this overlays the curated list.)
        send(0, notepatra::langkw::kTypeScriptExtraKW);
        send(1, notepatra::langkw::kTypeScriptTypes);
    }
    else if (lang == "Ruby") {
        send(0, notepatra::langkw::kRubyKW);
        // QsciLexerRuby doesn't expose a useful secondary slot; builtins
        // would dilute the keyword colour if forced into slot 0. Skip.
    }
    else if (lang == "PHP") {
        // PHP files are tokenised by QsciLexerHTML. Slot 4 = PHP keywords.
        // Also keep HTML slots 0/1 populated for embedded markup.
        send(0, notepatra::langkw::kHtmlTags);
        send(1, notepatra::langkw::kJavaScriptKW);
        send(4, notepatra::langkw::kPhpKW);
    }
    else if (lang == "Lua") {
        send(0, notepatra::langkw::kLuaKW);
        send(1, notepatra::langkw::kLuaBuiltins);
    }
    else if (lang == "Bash") {
        send(0, notepatra::langkw::kBashKW);
        // QsciLexerBash doesn't really use a secondary slot; builtins
        // mixed with keywords paints them all the same colour, which is
        // closer to what users expect for shell.
    }
    else if (lang == "C") {
        send(0, notepatra::langkw::kCKW);
        send(1, notepatra::langkw::kCTypes);
        // Preprocessor and builtins are tokenised separately by Scintilla;
        // slots 2/3 aren't reliably wired in QsciLexerCPP for plain C.
    }
    else if (lang == "C++") {
        send(0, notepatra::langkw::kCppKW);
        send(1, notepatra::langkw::kCppTypes);
    }
    else if (lang == "C#") {
        send(0, notepatra::langkw::kCSharpKW);
        send(1, notepatra::langkw::kCSharpTypes);
    }
    else if (lang == "Java") {
        send(0, notepatra::langkw::kJavaKW);
        send(1, notepatra::langkw::kJavaTypes);
    }
    else if (lang == "HTML") {
        send(0, notepatra::langkw::kHtmlTags);
        send(1, notepatra::langkw::kJavaScriptKW);
    }
    else if (lang == "XML") {
        // Defensive: XML lexer is structural (tags/attrs are styled by
        // position, not by keyword set). Leave default behaviour alone.
    }
    else if (lang == "CSS") {
        send(0, notepatra::langkw::kCssProperties);
        send(1, notepatra::langkw::kCssPseudoClasses);
        // Slot 2 (CSS2/3 props) defensively skipped — the QsciLexerCSS
        // implementation in QScintilla 2.14 routes slot 0 to colour ALL
        // recognised properties uniformly. Putting at-rules here would
        // double-stain. At-rules render via @-prefix style in the lexer.
    }
    else if (lang == "YAML") {
        send(0, notepatra::langkw::kYamlKW);
    }
    else if (lang == "JSON") {
        // QsciLexerJSON is not keyword-driven (true/false/null are painted
        // by the lexer's own constant style, not slot 0). Skip — sending
        // the string is a no-op but adds confusion.
    }
    else if (lang == "JSON5") {
        // LexerJson5 is a QsciLexerJSON subclass — same no-op semantics.
    }
    else if (lang == "TOML") {
        send(0, notepatra::langkw::kTomlKW);
    }
    else if (lang == "PowerShell") {
        send(0, notepatra::langkw::kPowerShellKW);
        send(1, notepatra::langkw::kPowerShellCmdlets);
    }
    else if (lang == "Rust") {
        send(0, notepatra::langkw::kRustKW);
        send(1, notepatra::langkw::kRustTypes);
    }
    else if (lang == "Go") {
        send(0, notepatra::langkw::kGoKW);
        send(1, notepatra::langkw::kGoTypes);
    }
    else if (lang == "Kotlin") {
        send(0, notepatra::langkw::kKotlinKW);
        send(1, notepatra::langkw::kKotlinTypes);
    }
    else if (lang == "Swift") {
        send(0, notepatra::langkw::kSwiftKW);
        send(1, notepatra::langkw::kSwiftTypes);
    }
    else if (lang == "Dockerfile") {
        send(0, notepatra::langkw::kDockerfileKW);
    }
    else if (lang == "Makefile") {
        send(0, notepatra::langkw::kMakefileKW);
    }
    // Default: nothing extra to send — language uses lexer-bundled keywords.
}
