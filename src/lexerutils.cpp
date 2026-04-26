#include "lexerutils.h"

#include <QFileInfo>
#include <QHash>

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
        {"html", "HTML"}, {"htm", "HTML"}, {"xhtml", "HTML"},
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
        {"tex", "TeX"}, {"latex", "TeX"}, {"bib", "TeX"}, {"cls", "TeX"}, {"sty", "TeX"},
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
        {"r", "Bash"}, {"rmd", "Markdown"}, // R: Bash lexer handles # comments + identifiers passably until we ship a custom R lexer
        {"hex", "IntelHex"}, {"ihex", "IntelHex"},
        {"srec", "SRecord"}, {"s19", "SRecord"}, {"s28", "SRecord"},
        {"log", "Plain Text"}, {"out", "Plain Text"}, {"txt", "Plain Text"},
        {"csv", "Plain Text"}, {"tsv", "Plain Text"},
        {"dockerignore", "Bash"}, {"gitignore", "Bash"},
        // Modern languages without dedicated QScintilla lexers — routed
        // to the closest existing lexer for syntax that mostly works.
        // Custom keyword overrides are TODO for v0.1.27+; for now this
        // is a 70-90% solution per language.
        {"dart", "JavaScript"},      // C-family syntax, similar braces/strings
        {"zig", "C++"},              // C-family
        {"zon", "C++"},              // Zig Object Notation -- tiny config files
        {"nim", "Python"},           // Indent-based + Python-like syntax
        {"nims", "Python"},          // NimScript
        {"ex", "Ruby"}, {"exs", "Ruby"},   // Elixir -- uses do/end like Ruby
        {"erl", "Perl"},             // Erlang -- closer to Perl than anything else
        {"hrl", "Perl"},             // Erlang headers
        {"cr", "Ruby"},              // Crystal -- explicit Ruby-inspired
        {"hs", "Bash"},              // Haskell -- # not used but Bash lexer is least-bad
        {"lhs", "Bash"},             // Literate Haskell
        {"ml", "Bash"}, {"mli", "Bash"},   // OCaml -- (* *) comments don't fit any lexer
        {"fs", "C#"}, {"fsx", "C#"}, {"fsi", "C#"},   // F# -- closer to C# than anything
        {"clj", "Lua"}, {"cljs", "Lua"}, {"cljc", "Lua"}, {"edn", "Lua"},   // Clojure -- ; comments
        {"elm", "Bash"},             // Elm -- # comments don't apply but braces help
        {"jl", "Python"},            // Julia -- syntactically Python-flavoured
        {"v", "Verilog"},            // V (lang) -- conflicts with Verilog (.v); Verilog wins
        {"vlang", "C++"},            // V language unique extension
        {"odin", "C++"},             // Odin -- C-family
        {"sb", "Bash"}, {"sbn", "Bash"},   // SmallBASIC / scratch
        {"awk", "Bash"},             // AWK -- # comments + similar feel
    };

    static const QHash<QString, QString> nameMap = {
        {"Makefile", "Makefile"}, {"makefile", "Makefile"}, {"GNUmakefile", "Makefile"},
        {"Dockerfile", "Bash"}, {"Vagrantfile", "Ruby"}, {"Rakefile", "Ruby"},
        {"Gemfile", "Ruby"}, {"Podfile", "Ruby"},
        {"CMakeLists.txt", "CMake"}, {"meson.build", "Python"},
        {".bashrc", "Bash"}, {".bash_profile", "Bash"}, {".zshrc", "Bash"},
        {".profile", "Bash"}, {".gitignore", "Bash"}, {".dockerignore", "Bash"},
        {".editorconfig", "Properties"}, {".env", "Properties"},
        {"Cargo.toml", "YAML"}, {"Cargo.lock", "YAML"},
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

    return nullptr;
}
