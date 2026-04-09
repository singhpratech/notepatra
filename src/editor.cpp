#include "editor.h"
#include "rustbridge.h"

// ALL 45 QScintilla lexers
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexerpascal.h>
// Optional lexers — available in QScintilla 2.14+ only
// Use __has_include to gracefully handle older versions
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

#include <QFont>
#include <QColor>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QMouseEvent>
#include "gitgutter.h"

Editor::Editor(QWidget *parent) : QsciScintilla(parent) {
    setupEditor();
    setupMargins();

    connect(this, &QsciScintilla::cursorPositionChanged, this, &Editor::onCursorMoved);
    connect(this, &QsciScintilla::marginClicked, this, &Editor::onMarginClicked);
}

void Editor::setupEditor() {
    QFont font("Consolas", 11);
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setMarginsFont(font);
    setUtf8(true);
    setCaretLineVisible(true);
    setCaretLineBackgroundColor(QColor("#E8F5E9"));  // pastel green
    setCaretWidth(2);

    // Explicit colors — black text on white background
    setPaper(QColor("#FFFFFF"));
    setColor(QColor("#000000"));
    setMarginsBackgroundColor(QColor("#E4E4E4"));
    setMarginsForegroundColor(QColor("#2B91AF"));
    setEdgeMode(QsciScintilla::EdgeLine);
    setEdgeColumn(120);
    setWrapMode(QsciScintilla::WrapNone);
    setWhitespaceVisibility(QsciScintilla::WsInvisible);
    setEolVisibility(false);
    setEolMode(QsciScintilla::EolUnix);
    setIndentationsUseTabs(false);
    setTabWidth(4);
    setIndentationGuides(true);
    setAutoIndent(true);
    setBackspaceUnindents(true);
    setTabIndents(true);
    setBraceMatching(QsciScintilla::StrictBraceMatch);
    setMatchedBraceBackgroundColor(QColor("#FFCCCC"));   // light red background
    setMatchedBraceForegroundColor(QColor("#CC0000"));   // dark red text
    setUnmatchedBraceBackgroundColor(QColor("#FF0000")); // bright red = unmatched
    setUnmatchedBraceForegroundColor(QColor("#FFFFFF")); // white text on red

    // Sticky scrolling — slower scroll speed, scroll past end
    SendScintilla(SCI_SETMOUSEDWELLTIME, 200);
    SendScintilla(SCI_SETENDATLASTLINE, 0);  // allow scrolling past last line
    SendScintilla(SCI_SETSCROLLWIDTH, 1);
    SendScintilla(SCI_SETSCROLLWIDTHTRACKING, 1);

    setAutoCompletionSource(QsciScintilla::AcsAll);
    setAutoCompletionThreshold(3);
    setAutoCompletionCaseSensitivity(false);
    setAutoCompletionReplaceWord(true);
    setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
}

void Editor::setupMargins() {
    // Line numbers
    setMarginType(0, QsciScintilla::NumberMargin);
    setMarginWidth(0, "00000");
    setMarginLineNumbers(0, true);

    // Fold margin
    setMarginType(2, QsciScintilla::SymbolMargin);
    setMarginWidth(2, 14);
    setMarginSensitivity(2, true);

    // Bookmark margin
    setMarginType(1, QsciScintilla::SymbolMargin);
    setMarginWidth(1, 16);
    setMarginSensitivity(1, true);
    markerDefine(QsciScintilla::Circle, 0);
    setMarkerForegroundColor(QColor("#FF0000"), 0);
    setMarkerBackgroundColor(QColor("#FF0000"), 0);

    // Git gutter markers (margin 3)
    setMarginType(3, QsciScintilla::SymbolMargin);
    setMarginWidth(3, 4);
    setMarginSensitivity(3, false);
    // Marker 1 = git added (green bar)
    markerDefine(QsciScintilla::Background, 1);
    setMarkerBackgroundColor(QColor("#4CAF50"), 1);
    // Marker 2 = git modified (yellow bar)
    markerDefine(QsciScintilla::Background, 2);
    setMarkerBackgroundColor(QColor("#FFC107"), 2);
    // Marker 3 = git deleted (red bar)
    markerDefine(QsciScintilla::Background, 3);
    setMarkerBackgroundColor(QColor("#F44336"), 3);

    // Setup indicator 9 for double-click word highlight (light orange)
    SendScintilla(SCI_INDICSETSTYLE, 9, INDIC_ROUNDBOX);
    SendScintilla(SCI_INDICSETFORE, 9, QColor("#E8A848").rgb() & 0xFFFFFF);
    SendScintilla(SCI_INDICSETALPHA, 9, 70);
    SendScintilla(SCI_INDICSETOUTLINEALPHA, 9, 140);
}

void Editor::mouseDoubleClickEvent(QMouseEvent *event) {
    QsciScintilla::mouseDoubleClickEvent(event);
    // After default double-click selects the word, highlight all occurrences
    if (hasSelectedText()) {
        QString word = selectedText().trimmed();
        if (!word.isEmpty() && word.length() > 1) {
            highlightAllOccurrences(word);
        }
    }
}

void Editor::highlightAllOccurrences(const QString &word) {
    // Clear previous highlights
    SendScintilla(SCI_SETINDICATORCURRENT, 9);
    QByteArray fullText = text().toUtf8();
    SendScintilla(SCI_INDICATORCLEARRANGE, 0, fullText.size());

    // Find all occurrences using Rust Aho-Corasick (fast)
    auto positions = RustCore::findAll(text(), word, false, true, true);

    QByteArray wordBytes = word.toUtf8();
    int wordLen = wordBytes.size();

    for (auto pos : positions) {
        SendScintilla(SCI_INDICATORFILLRANGE, (int)pos, wordLen);
    }
}

bool Editor::loadFile(const QString &path) {
    // Use Rust core for memory-safe file loading
    auto result = RustCore::loadFile(path);

    if (result.status == 3) {
        QMessageBox::warning(this, "Error", result.errorMsg);
        return false;
    }

    if (result.status == 1 || result.status == 2) {
        // Binary or too large — show info text
        setText(result.text);
        setReadOnly(true);
        m_encoding = result.encoding;
        m_language = "Plain Text";
        m_eolName = "N/A";
        m_filePath = path;
        setModified(false);
        return true;
    }

    m_filePath = path;
    m_encoding = result.encoding;

    switch (result.eolMode) {
        case 1: setEolMode(QsciScintilla::EolWindows); m_eolName = "Windows (CR LF)"; break;
        case 2: setEolMode(QsciScintilla::EolMac); m_eolName = "Macintosh (CR)"; break;
        default: setEolMode(QsciScintilla::EolUnix); m_eolName = "Unix (LF)"; break;
    }

    setText(result.text);
    setModified(false);

    if (result.truncated) {
        setReadOnly(true);
    }

    // Detect language from extension
    QFileInfo fi(path);
    QString ext = fi.suffix().toLower();
    QString name = fi.fileName();

    // Disable lexer for large files
    if (result.fileSize > 50 * 1024 * 1024) {
        applyLexer("Plain Text");
        setAutoCompletionSource(QsciScintilla::AcsNone);
        setBraceMatching(QsciScintilla::NoBraceMatch);
    } else {
        // Map extensions to languages — 100+ file types, ALL available lexers
        static const QHash<QString, QString> extMap = {
            // Python
            {"py", "Python"}, {"pyw", "Python"}, {"pyx", "Python"}, {"pyi", "Python"},
            {"pxd", "Python"}, {"ipynb", "JSON"}, {"sage", "Python"}, {"bzl", "Python"},
            // JavaScript / TypeScript / CoffeeScript
            {"js", "JavaScript"}, {"mjs", "JavaScript"}, {"cjs", "JavaScript"},
            {"jsx", "JavaScript"}, {"ts", "JavaScript"}, {"tsx", "JavaScript"},
            {"coffee", "CoffeeScript"}, {"litcoffee", "CoffeeScript"},
            // C / C++ / Objective-C
            {"c", "C"}, {"h", "C"},
            {"cpp", "C++"}, {"cxx", "C++"}, {"cc", "C++"}, {"hpp", "C++"},
            {"hxx", "C++"}, {"hh", "C++"}, {"ino", "C++"},
            {"m", "C++"}, {"mm", "C++"},
            // C#
            {"cs", "C#"},
            // Java / Kotlin / Scala / Groovy
            {"java", "Java"}, {"kt", "Java"}, {"kts", "Java"},
            {"scala", "Java"}, {"groovy", "Java"}, {"gradle", "Java"},
            // D
            {"d", "D"}, {"di", "D"},
            // Rust / Go / Swift (use C++ — closest braces syntax)
            {"rs", "C++"}, {"go", "C++"}, {"swift", "C++"},
            // Web
            {"html", "HTML"}, {"htm", "HTML"}, {"xhtml", "HTML"},
            {"vue", "HTML"}, {"svelte", "HTML"}, {"jsp", "HTML"},
            {"erb", "HTML"}, {"ejs", "HTML"}, {"hbs", "HTML"},
            {"twig", "HTML"}, {"jinja", "HTML"}, {"jinja2", "HTML"},
            {"php", "HTML"}, {"phtml", "HTML"},
            // CSS
            {"css", "CSS"}, {"scss", "CSS"}, {"sass", "CSS"}, {"less", "CSS"},
            // XML / Config
            {"xml", "XML"}, {"svg", "XML"}, {"xsl", "XML"}, {"xsd", "XML"},
            {"plist", "XML"}, {"rss", "XML"}, {"atom", "XML"}, {"wsdl", "XML"},
            {"xaml", "XML"}, {"csproj", "XML"}, {"vcxproj", "XML"}, {"sln", "XML"},
            // JSON
            {"json", "JSON"}, {"jsonc", "JSON"}, {"geojson", "JSON"},
            {"webmanifest", "JSON"}, {"har", "JSON"},
            // SQL — all variants use same lexer
            {"sql", "SQL"}, {"ddl", "SQL"}, {"dml", "SQL"},
            {"pgsql", "SQL"}, {"plsql", "SQL"}, {"tsql", "SQL"},
            {"mysql", "SQL"}, {"sqlite", "SQL"}, {"hql", "SQL"},
            {"cql", "SQL"}, {"psql", "SQL"},
            // Shell
            {"sh", "Bash"}, {"bash", "Bash"}, {"zsh", "Bash"}, {"fish", "Bash"},
            {"ksh", "Bash"}, {"csh", "Bash"}, {"tcsh", "Bash"},
            // Batch / PowerShell
            {"bat", "Batch"}, {"cmd", "Batch"}, {"ps1", "Batch"}, {"psm1", "Batch"},
            // Ruby
            {"rb", "Ruby"}, {"rake", "Ruby"}, {"gemspec", "Ruby"}, {"rbw", "Ruby"},
            // Perl
            {"pl", "Perl"}, {"pm", "Perl"}, {"pod", "Perl"}, {"t", "Perl"},
            // Lua
            {"lua", "Lua"}, {"luau", "Lua"}, {"wlua", "Lua"},
            // TCL
            {"tcl", "TCL"}, {"tk", "TCL"},
            // Fortran
            {"f", "Fortran"}, {"f90", "Fortran"}, {"f95", "Fortran"},
            {"f03", "Fortran"}, {"for", "Fortran"}, {"fpp", "Fortran"},
            {"f77", "Fortran77"},
            // MATLAB / Octave
            {"m", "Octave"}, {"mat", "Matlab"}, {"oct", "Octave"},
            // Assembly
            {"asm", "ASM"}, {"s", "ASM"}, {"S", "ASM"},
            {"nasm", "NASM"}, {"masm", "MASM"},
            // Verilog / VHDL
            {"v", "Verilog"}, {"sv", "Verilog"}, {"svh", "Verilog"},
            {"vhd", "VHDL"}, {"vhdl", "VHDL"},
            // LaTeX / PostScript
            {"tex", "TeX"}, {"latex", "TeX"}, {"bib", "TeX"}, {"cls", "TeX"}, {"sty", "TeX"},
            {"ps", "PostScript"}, {"eps", "PostScript"},
            // IDL
            {"idl", "IDL"}, {"pro", "IDL"},
            // Properties / INI
            {"ini", "Properties"}, {"cfg", "Properties"}, {"conf", "Properties"},
            {"properties", "Properties"}, {"env", "Properties"},
            {"editorconfig", "Properties"}, {"gitconfig", "Properties"},
            // POV-Ray
            {"pov", "POV"}, {"inc", "POV"},
            // Spice
            {"spice", "Spice"}, {"cir", "Spice"},
            // Markdown
            {"md", "Markdown"}, {"markdown", "Markdown"}, {"mkd", "Markdown"},
            {"rst", "Markdown"},
            // YAML
            {"yml", "YAML"}, {"yaml", "YAML"},
            // TOML (use Properties)
            {"toml", "Properties"},
            // Diff / Patch
            {"diff", "Diff"}, {"patch", "Diff"},
            // Pascal / Delphi
            {"pas", "Pascal"}, {"pp", "Pascal"}, {"dpr", "Pascal"}, {"dpk", "Pascal"},
            // CMake
            {"cmake", "CMake"},
            // AVS
            {"avs", "AVS"}, {"avsi", "AVS"},
            // R (use Octave — similar)
            {"r", "Octave"}, {"rmd", "Markdown"},
            // Hex formats
            {"hex", "IntelHex"}, {"ihex", "IntelHex"},
            {"srec", "SRecord"}, {"s19", "SRecord"}, {"s28", "SRecord"},
            // Log / Text
            {"log", "Plain Text"}, {"out", "Plain Text"}, {"txt", "Plain Text"},
            // Data
            {"csv", "Plain Text"}, {"tsv", "Plain Text"},
            // Docker / Git
            {"dockerignore", "Bash"}, {"gitignore", "Bash"},
        };

        // Map special filenames
        static const QHash<QString, QString> nameMap = {
            {"Makefile", "Makefile"}, {"makefile", "Makefile"}, {"GNUmakefile", "Makefile"},
            {"Dockerfile", "Bash"}, {"Vagrantfile", "Ruby"}, {"Rakefile", "Ruby"},
            {"Gemfile", "Ruby"}, {"Podfile", "Ruby"},
            {"CMakeLists.txt", "CMake"}, {"meson.build", "Python"},
            {".bashrc", "Bash"}, {".bash_profile", "Bash"}, {".zshrc", "Bash"},
            {".profile", "Bash"}, {".gitignore", "Bash"}, {".dockerignore", "Bash"},
            {".editorconfig", "Bash"}, {".env", "Bash"},
            {"Cargo.toml", "YAML"}, {"Cargo.lock", "YAML"},
            {"package.json", "JSON"}, {"tsconfig.json", "JSON"},
            {"composer.json", "JSON"}, {".eslintrc", "JSON"},
            {"requirements.txt", "Plain Text"}, {"go.mod", "Bash"}, {"go.sum", "Plain Text"},
        };

        QString lang = nameMap.value(name, extMap.value(ext, "Plain Text"));

        // JSON lexer can't handle broken/invalid JSON — shows white text.
        // Use JavaScript lexer instead which handles {key: value} syntax fine.
        // NOTE: trimmed() already strips whitespace including \r and \n, so a
        // valid JSON file like "{\n  \"key\":...}" becomes "{\"key\":...}" after
        // trim. Just check whether the FIRST non-whitespace char is { or [ and
        // whether anything quoted appears in the first 200 chars — covers
        // CRLF, LF, BOM, and indented files alike on every platform.
        if (lang == "JSON") {
            QString trimmed = result.text.trimmed();
            bool startsBrace = trimmed.startsWith('{') || trimmed.startsWith('[');
            bool hasQuoted   = trimmed.left(200).contains('"');
            if (!startsBrace || !hasQuoted) {
                lang = "JavaScript";  // JS lexer handles unquoted keys, single quotes etc
            }
        }

        applyLexer(lang);
    }

    return true;
}

bool Editor::saveFile(const QString &path) {
    QString savePath = path.isEmpty() ? m_filePath : path;
    if (savePath.isEmpty()) return false;

    bool ok = RustCore::saveFile(savePath, text(), m_encoding);
    if (ok) {
        m_filePath = savePath;
        setModified(false);
    }
    return ok;
}

void Editor::setLanguage(const QString &lang) {
    applyLexer(lang);
}

void Editor::applyLexer(const QString &lang) {
    m_language = lang;
    QsciLexer *lexer = nullptr;

    QFont font("Consolas", 11);
    font.setStyleHint(QFont::Monospace);

    // 45 languages — ALL available QScintilla lexers
    if (lang == "Python") lexer = new QsciLexerPython(this);
    else if (lang == "JavaScript") lexer = new QsciLexerJavaScript(this);
#ifdef HAS_LEXER_COFFEESCRIPT
    else if (lang == "CoffeeScript") lexer = new QsciLexerCoffeeScript(this);
#endif
    else if (lang == "C" || lang == "C++") lexer = new QsciLexerCPP(this);
    else if (lang == "C#") lexer = new QsciLexerCSharp(this);
#ifdef HAS_LEXER_D
    else if (lang == "D") lexer = new QsciLexerD(this);
#endif
    else if (lang == "Java") lexer = new QsciLexerJava(this);
    else if (lang == "HTML" || lang == "PHP") lexer = new QsciLexerHTML(this);
    else if (lang == "CSS") lexer = new QsciLexerCSS(this);
    else if (lang == "XML") lexer = new QsciLexerXML(this);
    else if (lang == "JSON") lexer = new QsciLexerJSON(this);
    else if (lang == "SQL") lexer = new QsciLexerSQL(this);
    else if (lang == "Bash") lexer = new QsciLexerBash(this);
    else if (lang == "Batch") lexer = new QsciLexerBatch(this);
    else if (lang == "Ruby") lexer = new QsciLexerRuby(this);
    else if (lang == "Perl") lexer = new QsciLexerPerl(this);
    else if (lang == "Lua") lexer = new QsciLexerLua(this);
#ifdef HAS_LEXER_TCL
    else if (lang == "TCL") lexer = new QsciLexerTCL(this);
#endif
#ifdef HAS_LEXER_FORTRAN
    else if (lang == "Fortran") lexer = new QsciLexerFortran(this);
#endif
#ifdef HAS_LEXER_FORTRAN
    else if (lang == "Fortran77") lexer = new QsciLexerFortran77(this);
#endif
#ifdef HAS_LEXER_MATLAB
    else if (lang == "Matlab") lexer = new QsciLexerMatlab(this);
#endif
#ifdef HAS_LEXER_MATLAB
    else if (lang == "Octave") lexer = new QsciLexerOctave(this);
#endif
#ifdef HAS_LEXER_IDL
    else if (lang == "IDL") lexer = new QsciLexerIDL(this);
#endif
#ifdef HAS_LEXER_NASM
    else if (lang == "ASM" || lang == "NASM") lexer = new QsciLexerNASM(this);
#endif
#ifdef HAS_LEXER_MASM
    else if (lang == "MASM") lexer = new QsciLexerMASM(this);
#endif
#ifdef HAS_LEXER_VERILOG
    else if (lang == "Verilog") lexer = new QsciLexerVerilog(this);
#endif
#ifdef HAS_LEXER_VHDL
    else if (lang == "VHDL") lexer = new QsciLexerVHDL(this);
#endif
#ifdef HAS_LEXER_TEX
    else if (lang == "TeX") lexer = new QsciLexerTeX(this);
#endif
#ifdef HAS_LEXER_POSTSCRIPT
    else if (lang == "PostScript") lexer = new QsciLexerPostScript(this);
#endif
#ifdef HAS_LEXER_POV
    else if (lang == "POV") lexer = new QsciLexerPOV(this);
#endif
#ifdef HAS_LEXER_SPICE
    else if (lang == "Spice") lexer = new QsciLexerSpice(this);
#endif
#ifdef HAS_LEXER_AVS
    else if (lang == "AVS") lexer = new QsciLexerAVS(this);
#endif
#ifdef HAS_LEXER_PROPERTIES
    else if (lang == "Properties") lexer = new QsciLexerProperties(this);
#endif
#ifdef HAS_LEXER_PO
    else if (lang == "PO") lexer = new QsciLexerPO(this);
#endif
#ifdef HAS_LEXER_INTELHEX
    else if (lang == "IntelHex") lexer = new QsciLexerIntelHex(this);
#endif
#ifdef HAS_LEXER_SREC
    else if (lang == "SRecord") lexer = new QsciLexerSRec(this);
#endif
    else if (lang == "Markdown") lexer = new QsciLexerMarkdown(this);
    else if (lang == "YAML") lexer = new QsciLexerYAML(this);
    else if (lang == "Diff") lexer = new QsciLexerDiff(this);
    else if (lang == "Pascal") lexer = new QsciLexerPascal(this);
    else if (lang == "CMake") lexer = new QsciLexerCMake(this);
    else if (lang == "Makefile") lexer = new QsciLexerMakefile(this);

    if (lexer) {
        // Set lexer first so its default styles are initialised
        lexer->setDefaultFont(font);
        lexer->setDefaultPaper(QColor("#FFFFFF"));
        lexer->setDefaultColor(QColor("#000000"));
        setLexer(lexer);
        // Apply Notepad++ default palette — Windows default QScintilla styles
        // sometimes render with no visible keyword color, so paint them ourselves.
        applyNotepadPlusPalette(lexer, font);
        setPaper(QColor("#FFFFFF"));
    } else {
        setLexer(nullptr);
        setFont(font);
        setPaper(QColor("#FFFFFF"));
        setColor(QColor("#000000"));
    }

    if (!m_themeName.isEmpty()) applyTheme(m_themeName);
}

// ═══════════════════════════════════════════════════════════════════════
// Notepad++ default color palette — matches stylers.xml from Notepad++ 8.x
// Applied per-style using the lexer's own description() to identify styles,
// so this works across ALL 40+ QScintilla lexers without hard-coding constants.
// ═══════════════════════════════════════════════════════════════════════
void Editor::applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont) {
    if (!lexer) return;

    // Notepad++ "Default" theme colors
    const QColor npPaper     (0xFF, 0xFF, 0xFF);  // White background
    const QColor npText      (0x00, 0x00, 0x00);  // Black text
    const QColor npKeyword   (0x00, 0x00, 0xFF);  // Blue bold
    const QColor npKeyword2  (0x80, 0x00, 0x80);  // Purple (KEYWORD2 / type)
    const QColor npComment   (0x00, 0x80, 0x00);  // Green italic
    const QColor npNumber    (0xFF, 0x80, 0x00);  // Orange
    const QColor npString    (0x80, 0x80, 0x80);  // Gray
    const QColor npChar      (0x80, 0x80, 0x80);  // Gray
    const QColor npOperator  (0x00, 0x00, 0x00);  // Black bold
    const QColor npPreproc   (0x80, 0x40, 0x00);  // Brown bold
    const QColor npRegex     (0x80, 0x00, 0x80);  // Purple
    const QColor npIdentifier(0x00, 0x00, 0x00);  // Black
    const QColor npClassName (0x00, 0x64, 0x80);  // Dark cyan (class/function)
    const QColor npDecorator (0xFF, 0x80, 0x00);  // Orange (decorators/attrs)
    const QColor npError     (0xFF, 0x00, 0x00);  // Red

    QFont regular = baseFont; regular.setBold(false); regular.setItalic(false);
    QFont bold    = baseFont; bold.setBold(true);    bold.setItalic(false);
    QFont italic  = baseFont; italic.setBold(false); italic.setItalic(true);

    // Paint all 128 possible style slots
    for (int i = 0; i < 128; ++i) {
        QString desc = lexer->description(i);
        if (desc.isEmpty()) continue;
        const QString d = desc.toLower();

        lexer->setPaper(npPaper, i);
        lexer->setFont(regular, i);
        QColor fg = npText;

        if (d.contains("keyword")) {
            // Secondary keyword sets — use purple to distinguish from primary blue
            if (d.contains("set 2") || d.contains("set2") ||
                d.contains("secondary") || d.contains("user")) {
                fg = npKeyword2;
            } else {
                fg = npKeyword;
            }
            lexer->setFont(bold, i);
        }
        else if (d.contains("comment")) {
            fg = npComment;
            lexer->setFont(italic, i);
        }
        else if (d.contains("number") || d.contains("numeric")) {
            fg = npNumber;
        }
        else if (d.contains("regex") || d.contains("regular expression")) {
            fg = npRegex;
        }
        else if (d.contains("string") || d.contains("char") ||
                 d.contains("literal") || d.contains("heredoc") ||
                 d.contains("backtick") || d.contains("verbatim")) {
            fg = (d.contains("char") ? npChar : npString);
        }
        else if (d.contains("preproc")) {
            fg = npPreproc;
            lexer->setFont(bold, i);
        }
        else if (d.contains("operator")) {
            fg = npOperator;
            lexer->setFont(bold, i);
        }
        else if (d.contains("decorator") || d.contains("attribute")) {
            fg = npDecorator;
            lexer->setFont(italic, i);
        }
        else if (d.contains("class") || d.contains("function") ||
                 d.contains("method") || d.contains("global")) {
            fg = npClassName;
            lexer->setFont(bold, i);
        }
        else if (d.contains("error") || d.contains("unclosed")) {
            fg = npError;
        }
        else if (d.contains("tag") || d.contains("element")) {
            // HTML/XML tag
            fg = npKeyword;
            lexer->setFont(bold, i);
        }
        else if (d.contains("entity")) {
            fg = npNumber;
        }
        else if (d.contains("header") || d.contains("header1") ||
                 d.contains("strong") || d.contains("bold")) {
            // Markdown
            fg = npKeyword;
            lexer->setFont(bold, i);
        }
        else if (d.contains("emphasis") || d.contains("italic")) {
            fg = npKeyword2;
            lexer->setFont(italic, i);
        }
        else if (d.contains("link") || d.contains("url")) {
            fg = npClassName;
        }
        // else: plain text / identifier — keep npText

        lexer->setColor(fg, i);
    }

    // Also set the default style (0) explicitly in case lexer skips it
    lexer->setPaper(npPaper, 0);
    lexer->setColor(npText, 0);
    lexer->setFont(regular, 0);
}

void Editor::applyTheme(const QString &themeName) {
    m_themeName = themeName;
    // Theme colors applied from mainwindow
}

void Editor::onCursorMoved(int line, int col) {
    emit cursorPositionUpdated(line + 1, col + 1);
    // Note: do NOT clear brace highlight here. QScintilla repaints the
    // brace highlight automatically when the caret lands on/next to a brace,
    // and clearing on every tiny movement made goToMatchingBrace() invisible.
}

void Editor::onMarginClicked(int margin, int line, Qt::KeyboardModifiers) {
    if (margin == 2) {
        foldLine(line);
    } else if (margin == 1) {
        if (markersAtLine(line) & 1)
            markerDelete(line, 0);
        else
            markerAdd(line, 0);
    }
}

void Editor::gotoLine(int line) {
    setCursorPosition(line - 1, 0);
    ensureLineVisible(line - 1);
}

void Editor::duplicateLine() {
    int line, col;
    getCursorPosition(&line, &col);
    QString lineText = text(line);
    insertAt(lineText, line + 1, 0);
}

void Editor::deleteLine() {
    int line, col;
    getCursorPosition(&line, &col);
    setSelection(line, 0, line + 1, 0);
    removeSelectedText();
}

void Editor::moveLineUp() {
    int line, col;
    getCursorPosition(&line, &col);
    if (line <= 0) return;

    beginUndoAction();
    QString current = text(line).trimmed();
    QString above = text(line - 1).trimmed();

    int endLine = (line < lines() - 1) ? line + 1 : line;
    int endCol = (line < lines() - 1) ? 0 : text(line).length();
    setSelection(line - 1, 0, endLine, endCol);

    QString replacement = current + "\n" + above;
    if (line < lines() - 1) replacement += "\n";
    replaceSelectedText(replacement);

    setCursorPosition(line - 1, col);
    endUndoAction();
}

void Editor::moveLineDown() {
    int line, col;
    getCursorPosition(&line, &col);
    if (line >= lines() - 1) return;

    beginUndoAction();
    QString current = text(line).trimmed();
    QString below = text(line + 1).trimmed();

    int endLine = (line + 1 < lines() - 1) ? line + 2 : line + 1;
    int endCol = (line + 1 < lines() - 1) ? 0 : text(line + 1).length();
    setSelection(line, 0, endLine, endCol);

    QString replacement = below + "\n" + current;
    if (line + 1 < lines() - 1) replacement += "\n";
    replaceSelectedText(replacement);

    setCursorPosition(line + 1, col);
    endUndoAction();
}

void Editor::toggleComment() {
    QsciLexer *lex = lexer();
    QString comment = "#";
    if (lex) {
        QString name = lex->metaObject()->className();
        if (name.contains("CPP") || name.contains("Java") || name.contains("JavaScript") ||
            name.contains("CSharp") || name.contains("JSON"))
            comment = "//";
        else if (name.contains("SQL") || name.contains("Lua"))
            comment = "--";
        else if (name.contains("Batch"))
            comment = "REM ";
    }

    int lineFrom, lineTo, colFrom, colTo;
    if (hasSelectedText()) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
        if (colTo == 0) lineTo--;
    } else {
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
    }

    beginUndoAction();
    bool allCommented = true;
    for (int i = lineFrom; i <= lineTo; i++) {
        if (!text(i).trimmed().startsWith(comment)) {
            allCommented = false;
            break;
        }
    }

    for (int i = lineFrom; i <= lineTo; i++) {
        if (allCommented) {
            QString line = text(i);
            int idx = line.indexOf(comment);
            if (idx >= 0) {
                int removeLen = comment.length();
                if (idx + removeLen < line.length() && line[idx + removeLen] == ' ')
                    removeLen++;
                setSelection(i, idx, i, idx + removeLen);
                removeSelectedText();
            }
        } else {
            insertAt(comment + " ", i, 0);
        }
    }
    endUndoAction();
}

void Editor::toggleWordWrap() {
    setWrapMode(wrapMode() == QsciScintilla::WrapNone
                    ? QsciScintilla::WrapWord
                    : QsciScintilla::WrapNone);
}

void Editor::toggleWhitespace() {
    setWhitespaceVisibility(whitespaceVisibility() == QsciScintilla::WsInvisible
                                ? QsciScintilla::WsVisible
                                : QsciScintilla::WsInvisible);
}

void Editor::toggleEol() {
    setEolVisibility(!eolVisibility());
}

void Editor::clearBraceHighlight() {
    SendScintilla(SCI_BRACEHIGHLIGHT, (unsigned long)-1, (long)-1);
}

void Editor::goToMatchingBrace() {
    // Notepad++ behaviour: Ctrl+B moves the caret to the matching brace.
    // Pressing it again swivels back — because the caret is now ON the
    // other brace and BRACEMATCH returns the original position.
    int pos = (int)SendScintilla(SCI_GETCURRENTPOS);

    auto isBrace = [](int c) {
        return c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
    };

    // Probe order: char AT caret, then char BEFORE caret (caret can sit on either side)
    int bracePos = -1;
    int ch = (int)SendScintilla(SCI_GETCHARAT, (unsigned long)pos, (long)0);
    if (isBrace(ch)) {
        bracePos = pos;
    } else if (pos > 0) {
        ch = (int)SendScintilla(SCI_GETCHARAT, (unsigned long)(pos - 1), (long)0);
        if (isBrace(ch)) bracePos = pos - 1;
    }

    if (bracePos < 0) return;

    int matchPos = (int)SendScintilla(SCI_BRACEMATCH, (unsigned long)bracePos, (long)0);
    if (matchPos < 0) {
        SendScintilla(SCI_BRACEBADLIGHT, (unsigned long)bracePos);
        return;
    }

    // Move caret PAST the matching brace so the next Ctrl+B press finds it
    // (SCI_GETCHARAT at new caret position returns the brace we just moved to,
    // because caret lands immediately AFTER it).
    SendScintilla(SCI_GOTOPOS, (unsigned long)(matchPos + 1));
    ensureLineVisible((int)SendScintilla(SCI_LINEFROMPOSITION,
                                         (unsigned long)matchPos, (long)0));

    // Re-apply the visual brace highlight AFTER the goto (cursor-moved slot
    // clears it; we want it visible on the destination brace).
    SendScintilla(SCI_BRACEHIGHLIGHT, (unsigned long)bracePos, (long)matchPos);
}

void Editor::updateGitGutter() {
    // Clear existing git markers
    markerDeleteAll(1);
    markerDeleteAll(2);
    markerDeleteAll(3);

    if (m_filePath.isEmpty()) return;

    auto changes = GitGutter::getChangedLines(m_filePath, text());
    for (const auto &change : changes) {
        if (change.line > 0 && change.line <= lines()) {
            markerAdd(change.line - 1, change.status); // 1=added, 2=modified, 3=deleted
        }
    }
}
