#include "editor.h"
#include "lexerutils.h"
#include "rustbridge.h"
#include "npp_palette.h"
#include "fonts.h"
#include "themes.h"
#include "config.h"

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

#include <QFile>
#include <QFont>
#include <QColor>
#include <QFileInfo>
#include <QHash>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCodec>
#include <QToolTip>
#include "gitgutter.h"

namespace {

static bool themeIsDark(const QString &themeName) {
    return themeName.compare("Dark", Qt::CaseInsensitive) == 0 ||
           themeName.compare("Monokai", Qt::CaseInsensitive) == 0;
}

struct MeasurementTheme {
    QColor bg;
    QColor border;
    QColor tick;
    QColor text;
    QColor accent;
    QColor overlay;
    QColor overlayText;
};

static MeasurementTheme measurementThemeFor(const QString &themeName) {
    if (themeIsDark(themeName)) {
        return {
            QColor("#252526"),
            QColor("#3C3C3C"),
            QColor("#6C737C"),
            QColor("#C8CDD4"),
            QColor("#D7BA7D"),
            QColor(215, 186, 125, 48),
            QColor("#1E1E1E")
        };
    }

    return {
        QColor("#F4F1EA"),
        QColor("#D7D0C4"),
        QColor("#9A9389"),
        QColor("#4B4A46"),
        QColor("#B7791F"),
        QColor(183, 121, 31, 40),
        QColor("#3A2A14")
    };
}

} // namespace

class EditorRulerBand : public QWidget {
public:
    enum Axis { Horizontal, Vertical };

    EditorRulerBand(Axis axis, QWidget *parent = nullptr)
        : QWidget(parent), m_axis(axis) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setThemeName(const QString &themeName) {
        m_themeName = themeName;
        update();
    }

    void setScrollOffset(int offset) {
        m_scrollOffset = qMax(0, offset);
        update();
    }

    void setCrosshairPixel(int pixel) {
        m_crosshairPixel = pixel;
        update();
    }

    void clearCrosshairPixel() {
        m_crosshairPixel = -1;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        const MeasurementTheme theme = measurementThemeFor(m_themeName);

        painter.fillRect(rect(), theme.bg);
        painter.setPen(theme.border);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));

        painter.setPen(theme.tick);
        painter.setFont(notepatraUiFont(8));

        const int size = (m_axis == Horizontal) ? width() : height();
        for (int pos = 0; pos <= size; pos += 10) {
            const int logical = m_scrollOffset + pos;
            const bool major = (logical % 100) == 0;
            const bool mid = (logical % 50) == 0;
            const int tick = major ? 11 : (mid ? 8 : 5);

            if (m_axis == Horizontal) {
                painter.drawLine(pos, height() - 1, pos, height() - tick);
                if (major && pos + 24 < width()) {
                    painter.setPen(theme.text);
                    painter.drawText(pos + 2, 10, QString::number(logical));
                    painter.setPen(theme.tick);
                }
            } else {
                painter.drawLine(width() - 1, pos, width() - tick, pos);
                if (major && pos + 10 < height()) {
                    painter.save();
                    painter.translate(2, pos + 24);
                    painter.rotate(-90);
                    painter.setPen(theme.text);
                    painter.drawText(0, 0, QString::number(logical));
                    painter.restore();
                    painter.setPen(theme.tick);
                }
            }
        }

        if (m_crosshairPixel >= 0) {
            const int marker = m_crosshairPixel - m_scrollOffset;
            painter.setPen(QPen(theme.accent, 1));
            if (m_axis == Horizontal && marker >= 0 && marker < width()) {
                painter.drawLine(marker, 0, marker, height());
            } else if (m_axis == Vertical && marker >= 0 && marker < height()) {
                painter.drawLine(0, marker, width(), marker);
            }
        }
    }

private:
    Axis m_axis;
    QString m_themeName;
    int m_scrollOffset = 0;
    int m_crosshairPixel = -1;
};

class EditorCrosshairOverlay : public QWidget {
public:
    explicit EditorCrosshairOverlay(QWidget *parent = nullptr)
        : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setThemeName(const QString &themeName) {
        m_themeName = themeName;
        update();
    }

    void setCrosshair(const QPoint &viewportPos, const QPoint &documentPx) {
        m_viewportPos = viewportPos;
        m_documentPx = documentPx;
        m_visible = true;
        update();
    }

    void clearCrosshair() {
        m_visible = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        if (!m_visible) return;

        QPainter painter(this);
        const MeasurementTheme theme = measurementThemeFor(m_themeName);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(theme.accent, 1));
        painter.drawLine(m_viewportPos.x(), 0, m_viewportPos.x(), height());
        painter.drawLine(0, m_viewportPos.y(), width(), m_viewportPos.y());

        const QString label = QString("%1 px, %2 px").arg(m_documentPx.x()).arg(m_documentPx.y());
        painter.setFont(notepatraUiFont(9, QFont::DemiBold));
        QFontMetrics metrics(painter.font());
        QRect bubble(0, 0, metrics.horizontalAdvance(label) + 14, metrics.height() + 8);

        int bubbleX = m_viewportPos.x() + 10;
        int bubbleY = m_viewportPos.y() + 10;
        if (bubbleX + bubble.width() > width()) bubbleX = m_viewportPos.x() - bubble.width() - 10;
        if (bubbleY + bubble.height() > height()) bubbleY = m_viewportPos.y() - bubble.height() - 10;
        bubble.moveTopLeft(QPoint(qMax(4, bubbleX), qMax(4, bubbleY)));

        painter.setPen(Qt::NoPen);
        painter.setBrush(theme.overlay);
        painter.drawRoundedRect(bubble, 6, 6);
        painter.setPen(theme.overlayText);
        painter.drawText(bubble.adjusted(7, 0, -7, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }

private:
    QString m_themeName;
    QPoint m_viewportPos;
    QPoint m_documentPx;
    bool m_visible = false;
};

Editor::Editor(QWidget *parent) : QsciScintilla(parent) {
    setupEditor();
    setupMargins();

    viewport()->setMouseTracking(true);
    setMouseTracking(true);
    viewport()->installEventFilter(this);

    m_horizontalRuler = new EditorRulerBand(EditorRulerBand::Horizontal, this);
    m_verticalRuler = new EditorRulerBand(EditorRulerBand::Vertical, this);
    m_crosshairOverlay = new EditorCrosshairOverlay(viewport());
    m_rulerCorner = new QWidget(this);
    m_rulerCorner->setObjectName("editorRulerCorner");

    connect(this, &QsciScintilla::cursorPositionChanged, this, &Editor::onCursorMoved);
    connect(this, &QsciScintilla::marginClicked, this, &Editor::onMarginClicked);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]() { syncMeasurementUi(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this]() { syncMeasurementUi(); });

    setDocumentRulersVisible(Config::instance().showDocumentRulers);
    setCrosshairVisible(Config::instance().showCrosshair);
}

void Editor::setupEditor() {
    // v0.1.42 — every Config-controlled value is consumed by applyConfig().
    // setupEditor() now sets only the bits that don't have a Config knob
    // (brace-match colours, sticky-scroll, paper colour). Everything else
    // is applied through applyConfig() so the user's Preferences dialog
    // changes propagate via a single code path.
    setUtf8(true);

    setCaretLineBackgroundColor(QColor("#E8F5E9"));  // pastel green

    // Explicit colors — black text on white background
    setPaper(QColor("#FFFFFF"));
    setColor(QColor("#000000"));
    setMarginsBackgroundColor(QColor("#E4E4E4"));
    setMarginsForegroundColor(QColor("#2B91AF"));
    setWhitespaceVisibility(QsciScintilla::WsInvisible);
    setEolVisibility(false);
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

    setAutoCompletionCaseSensitivity(false);
    setAutoCompletionReplaceWord(true);

    // Apply every Config-controlled setting. Single source of truth.
    applyConfig();
}

// v0.1.42 — applyConfig() reads every Config field that affects the
// editor and applies it. Called by setupEditor() at construction, by
// the Preferences dialog after OK, and by anything else that mutates
// Config and needs to push the change to all open editors.
void Editor::applyConfig() {
    const auto &cfg = Config::instance();

    // Font (family + size)
    QFont font = notepatraCodeFont(cfg.fontSize > 0 ? cfg.fontSize : 11);
    if (cfg.smoothFont) font.setStyleStrategy(QFont::PreferAntialias);
    else                font.setStyleStrategy(QFont::NoAntialias);
    setFont(font);
    setMarginsFont(font);

    // Caret
    setCaretWidth(qBound(1, cfg.caretWidth, 3));
    setCaretLineVisible(cfg.highlightCurrentLine);

    // Tabs / indentation
    setIndentationsUseTabs(cfg.useTabs);
    setTabWidth(qMax(1, cfg.tabWidth));
    setIndentationGuides(cfg.showIndentGuides);
    setAutoIndent(cfg.autoIndent);

    // Wrap
    setWrapMode(cfg.wordWrap ? QsciScintilla::WrapWord : QsciScintilla::WrapNone);

    // Edge column ruler
    if (cfg.showEdge) {
        setEdgeMode(QsciScintilla::EdgeLine);
        setEdgeColumn(qMax(1, cfg.edgeColumn));
    } else {
        setEdgeMode(QsciScintilla::EdgeNone);
    }

    // Line-number margin
    setMarginLineNumbers(0, cfg.showLineNumbers);
    if (!cfg.showLineNumbers) setMarginWidth(0, 0);
    // (the actual margin width is computed by setupMargins() based on
    // line count; we just disable the lexer call when hidden)

    // Auto-completion
    setAutoCompletionSource(cfg.autoComplete ? QsciScintilla::AcsAll
                                              : QsciScintilla::AcsNone);
    setAutoCompletionThreshold(cfg.autoComplete
                                  ? qMax(1, cfg.autoCompleteThreshold) : -1);

    // Fold style
    QsciScintilla::FoldStyle fs = QsciScintilla::BoxedTreeFoldStyle;
    if (cfg.foldStyle == "CircleTree")  fs = QsciScintilla::CircledTreeFoldStyle;
    else if (cfg.foldStyle == "Plain")  fs = QsciScintilla::PlainFoldStyle;
    else if (cfg.foldStyle == "Boxed")  fs = QsciScintilla::BoxedFoldStyle;
    else if (cfg.foldStyle == "Circle") fs = QsciScintilla::CircledFoldStyle;
    else if (cfg.foldStyle == "None")   fs = QsciScintilla::NoFoldStyle;
    setFolding(fs, 2);

    // Default EOL for new documents — only applied when this editor has
    // no file path yet (i.e. it's a fresh "Untitled" buffer). Applying
    // it to a loaded file would silently change CRLF/LF/CR which the
    // user explicitly opened.
    if (m_filePath.isEmpty()) {
        if      (cfg.defaultEol == "Windows") { setEolMode(QsciScintilla::EolWindows); m_eolName = "Windows (CR LF)"; }
        else if (cfg.defaultEol == "Mac")     { setEolMode(QsciScintilla::EolMac);     m_eolName = "Macintosh (CR)"; }
        else                                  { setEolMode(QsciScintilla::EolUnix);    m_eolName = "Unix (LF)"; }
        emit eolModeChanged(m_eolName);
    }

    // Document rulers + crosshair (already had Config-driven setters)
    setDocumentRulersVisible(cfg.showDocumentRulers);
    setCrosshairVisible(cfg.showCrosshair);
}

// v0.1.42 — encoding API.
void Editor::setEncoding(const QString &name) {
    if (m_encoding == name) return;
    m_encoding = name;
    emit encodingChanged(m_encoding);
}

void Editor::convertEncoding(const QString &name) {
    // Keep the current QString text in memory (Qt strings are unicode);
    // only the LABEL changes. Next saveFile() writes bytes in the new
    // encoding. Marks the buffer dirty so the user is prompted to save.
    if (m_encoding == name) return;
    m_encoding = name;
    setModified(true);
    emit encodingChanged(m_encoding);
}

bool Editor::reloadWithEncoding(const QString &name, bool force) {
    if (m_filePath.isEmpty()) {
        // No file on disk to re-read from. Treat like convertEncoding.
        convertEncoding(name);
        return true;
    }
    if (isModified() && !force) return false;

    // Read raw bytes from disk and decode with the user's chosen codec.
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray bytes = f.readAll();
    f.close();

    QString decoded;
    bool addBom = false;
    if (name.compare("UTF-8 BOM", Qt::CaseInsensitive) == 0 ||
        name.compare("UTF-8-BOM", Qt::CaseInsensitive) == 0) {
        // Strip BOM if present, decode the rest as UTF-8.
        if (bytes.startsWith(QByteArray::fromHex("EFBBBF")))
            bytes.remove(0, 3);
        decoded = QString::fromUtf8(bytes);
        addBom = true;
    } else if (name.compare("UTF-8", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("EFBBBF")))
            bytes.remove(0, 3);
        decoded = QString::fromUtf8(bytes);
    } else {
        QTextCodec *codec = QTextCodec::codecForName(name.toUtf8());
        if (!codec) {
            // Common-name aliases to canonical Qt codec names.
            const QString lower = name.toLower();
            if (lower.contains("utf-16 le") || lower == "utf-16le")
                codec = QTextCodec::codecForName("UTF-16LE");
            else if (lower.contains("utf-16 be") || lower == "utf-16be")
                codec = QTextCodec::codecForName("UTF-16BE");
            else if (lower.contains("ansi") || lower.contains("windows-1252"))
                codec = QTextCodec::codecForName("Windows-1252");
            else if (lower.contains("iso-8859-1") || lower.contains("latin-1"))
                codec = QTextCodec::codecForName("ISO 8859-1");
        }
        if (!codec) return false;
        decoded = codec->toUnicode(bytes);
    }

    setText(decoded);
    setModified(false);
    m_encoding = addBom ? QStringLiteral("UTF-8 BOM") : name;
    emit encodingChanged(m_encoding);
    return true;
}

// v0.1.42 — set EOL by friendly name. Optionally converts existing text.
void Editor::setEolModeByName(const QString &name, bool convertExisting) {
    QsciScintilla::EolMode mode = QsciScintilla::EolUnix;
    QString display = "Unix (LF)";
    if (name.contains("Windows") || name.contains("CRLF") || name.contains("CR LF")) {
        mode = QsciScintilla::EolWindows; display = "Windows (CR LF)";
    } else if (name.contains("Mac") || (name.contains("CR") && !name.contains("LF"))) {
        mode = QsciScintilla::EolMac; display = "Macintosh (CR)";
    }
    setEolMode(mode);
    if (convertExisting) convertEols(mode);
    if (m_eolName != display) {
        m_eolName = display;
        emit eolModeChanged(m_eolName);
    }
}

// v0.1.42 — zoom that persists to Config.
void Editor::zoomInPersistent() {
    auto &cfg = Config::instance();
    cfg.fontSize = qBound(6, cfg.fontSize + 1, 48);
    cfg.save();
    applyConfig();
}
void Editor::zoomOutPersistent() {
    auto &cfg = Config::instance();
    cfg.fontSize = qBound(6, cfg.fontSize - 1, 48);
    cfg.save();
    applyConfig();
}
void Editor::zoomResetPersistent() {
    auto &cfg = Config::instance();
    cfg.fontSize = 11;
    cfg.save();
    applyConfig();
}

int Editor::horizontalPixelOffset() const {
    return (int)SendScintilla(SCI_GETXOFFSET);
}

int Editor::verticalPixelOffset() const {
    const int lineHeight = qMax(1, (int)SendScintilla(SCI_TEXTHEIGHT, 0));
    const int firstVisibleLine = (int)SendScintilla(SCI_GETFIRSTVISIBLELINE);
    return qMax(0, firstVisibleLine * lineHeight);
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
    // Disable lexer for large files
    if (result.fileSize > 50 * 1024 * 1024) {
        applyLexer("Plain Text");
        setAutoCompletionSource(QsciScintilla::AcsNone);
        setBraceMatching(QsciScintilla::NoBraceMatch);
    } else {
        applyLexer(detectLanguageFromPath(path, result.text));
    }

    return true;
}

bool Editor::saveFile(const QString &path) {
    QString savePath = path.isEmpty() ? m_filePath : path;
    if (savePath.isEmpty()) return false;

    // v0.1.42 — actually honour m_encoding when writing the file. Pre-
    // v0.1.42, the Encoding menu set a label on the editor but saveFile
    // only ever wrote UTF-8 bytes (RustCore::saveFile path was UTF-8-
    // centric). Now we encode the QString through the right QTextCodec
    // for the chosen encoding and write the bytes directly via QFile.
    // BOM bytes are prepended for UTF-8 BOM / UTF-16 LE / UTF-16 BE.
    const QString textOut = text();
    QByteArray bytes;
    const QString enc = m_encoding;

    if (enc.compare("UTF-8 BOM", Qt::CaseInsensitive) == 0 ||
        enc.compare("UTF-8-BOM", Qt::CaseInsensitive) == 0) {
        bytes = QByteArray::fromHex("EFBBBF");
        bytes += textOut.toUtf8();
    } else if (enc.compare("UTF-8", Qt::CaseInsensitive) == 0 || enc.isEmpty()) {
        bytes = textOut.toUtf8();
    } else {
        // Pick the right codec by name, with friendly aliases.
        QTextCodec *codec = QTextCodec::codecForName(enc.toUtf8());
        if (!codec) {
            const QString lower = enc.toLower();
            if (lower.contains("utf-16 le") || lower == "utf-16le")
                codec = QTextCodec::codecForName("UTF-16LE");
            else if (lower.contains("utf-16 be") || lower == "utf-16be")
                codec = QTextCodec::codecForName("UTF-16BE");
            else if (lower.contains("utf-16"))
                codec = QTextCodec::codecForName("UTF-16");
            else if (lower.contains("ansi") || lower.contains("windows-1252"))
                codec = QTextCodec::codecForName("Windows-1252");
            else if (lower.contains("iso-8859-1") || lower.contains("latin-1"))
                codec = QTextCodec::codecForName("ISO 8859-1");
        }
        if (!codec) {
            QMessageBox::warning(this, "Unknown encoding",
                QString("Could not find a codec for %1. Falling back to UTF-8.").arg(enc));
            bytes = textOut.toUtf8();
        } else {
            // QTextCodec::fromUnicode emits a BOM for UTF-16 by default.
            bytes = codec->fromUnicode(textOut);
        }
    }

    QFile f(savePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    qint64 wrote = f.write(bytes);
    f.close();
    if (wrote != bytes.size()) return false;

    m_filePath = savePath;
    setModified(false);
    return true;
}

void Editor::setLanguage(const QString &lang) {
    applyLexer(lang);
}

void Editor::applyLexer(const QString &lang) {
    m_language = lang;
    QsciLexer *lexer = nullptr;

    QFont font = notepatraCodeFont();

    lexer = createLexerForLanguage(lang, this);

    // Resolve theme so dark/monokai users get dark paper instead of the
    // hardcoded white that used to land here (the editor body painted
    // white on dark chrome — looked broken).
    const QString rawTheme = m_themeName.isEmpty() ? Config::instance().theme : m_themeName;
    QString themeName = rawTheme;
    if (themeName.compare("System", Qt::CaseInsensitive) == 0)
        themeName = detectSystemTheme();
    const bool darkTheme = themeName.compare("Dark", Qt::CaseInsensitive) == 0 ||
                           themeName.compare("Monokai", Qt::CaseInsensitive) == 0;
    const QColor editorPaper = (themeName.compare("Monokai", Qt::CaseInsensitive) == 0)
                                   ? QColor("#272822")
                                   : (darkTheme ? QColor("#1E1E1E") : QColor("#FFFFFF"));
    const QColor editorFg    = (themeName.compare("Monokai", Qt::CaseInsensitive) == 0)
                                   ? QColor("#F8F8F2")
                                   : (darkTheme ? QColor("#D4D4D4") : QColor("#000000"));

    if (lexer) {
        // Set lexer first so its default styles are initialised
        lexer->setDefaultFont(font);
        lexer->setDefaultPaper(editorPaper);
        lexer->setDefaultColor(editorFg);
        setLexer(lexer);
        // Apply Notepad++ default palette — Windows default QScintilla styles
        // sometimes render with no visible keyword color, so paint them ourselves.
        ::applyNotepadPlusPalette(lexer, font, themeName);
        setPaper(editorPaper);
    } else {
        setLexer(nullptr);
        setFont(font);
        setPaper(editorPaper);
        setColor(editorFg);
    }

    // ─── Re-apply brace match colors AFTER setLexer ─────────────────────
    // setLexer() resets STYLE_BRACELIGHT (34) and STYLE_BRACEBAD (35) to
    // Scintilla defaults, wiping out the red highlight colors that were
    // set in setupEditor(). Re-apply them here so brace matching keeps
    // working after every file load / language switch.
    setBraceMatching(QsciScintilla::StrictBraceMatch);
    setMatchedBraceBackgroundColor(QColor("#FFCCCC"));   // light red background
    setMatchedBraceForegroundColor(QColor("#CC0000"));   // dark red text
    setUnmatchedBraceBackgroundColor(QColor("#FF0000")); // bright red = unmatched
    setUnmatchedBraceForegroundColor(QColor("#FFFFFF")); // white text on red

    if (!m_themeName.isEmpty()) applyTheme(m_themeName);
}

// Delegate to the free function in npp_palette.cpp — factored out so
// test_palette.cpp can link it directly without pulling in Editor.cpp's
// rustbridge dependencies.
void Editor::applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont) {
    QString themeName = Config::instance().theme;
    if (themeName.compare("System", Qt::CaseInsensitive) == 0)
        themeName = detectSystemTheme();
    ::applyNotepadPlusPalette(lexer, baseFont, themeName);
}

void Editor::applyTheme(const QString &themeName) {
    m_themeName = themeName;
    const QMap<QString, Theme> themes = allThemes();
    const Theme theme = themes.contains(themeName) ? themes[themeName] : darkTheme();

    setPaper(theme.editorBg);
    setColor(theme.editorFg);
    setCaretLineBackgroundColor(theme.caretLine);
    setCaretForegroundColor(theme.caret);
    setSelectionBackgroundColor(theme.selection);
    setMarginsBackgroundColor(theme.marginBg);
    setMarginsForegroundColor(theme.marginFg);
    setFoldMarginColors(theme.foldBg, theme.foldBg);
    setMatchedBraceBackgroundColor(theme.matchedBraceBg);
    setMatchedBraceForegroundColor(theme.matchedBraceFg);

    if (auto *lex = lexer()) {
        lex->setDefaultPaper(theme.editorBg);
        lex->setDefaultColor(theme.editorFg);
        ::applyNotepadPlusPalette(lex, font(), themeName);
    }

    updateMeasurementTheme();
    syncMeasurementUi();
}

void Editor::setDocumentRulersVisible(bool visible) {
    m_showDocumentRulers = visible;
    syncMeasurementUi();
}

void Editor::setCrosshairVisible(bool visible) {
    m_showCrosshair = visible;
    if (!visible && m_crosshairOverlay) m_crosshairOverlay->clearCrosshair();
    syncMeasurementUi();
}

void Editor::updateMeasurementTheme() {
    const QString themeName = m_themeName.isEmpty() ? Config::instance().theme : m_themeName;
    const MeasurementTheme theme = measurementThemeFor(themeName);

    if (m_horizontalRuler) m_horizontalRuler->setThemeName(themeName);
    if (m_verticalRuler) m_verticalRuler->setThemeName(themeName);
    if (m_crosshairOverlay) m_crosshairOverlay->setThemeName(themeName);
    if (m_rulerCorner) {
        m_rulerCorner->setStyleSheet(QString(
            "background: %1; border-right: 1px solid %2; border-bottom: 1px solid %2;")
            .arg(theme.bg.name(), theme.border.name()));
        m_rulerCorner->setToolTip("Pixel rulers");
    }
}

void Editor::syncMeasurementUi() {
    const int rulerTop = m_showDocumentRulers ? 22 : 0;
    const int rulerLeft = m_showDocumentRulers ? 30 : 0;
    setViewportMargins(rulerLeft, rulerTop, 0, 0);

    const int vScrollbarWidth = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0;
    const int hScrollbarHeight = horizontalScrollBar()->isVisible() ? horizontalScrollBar()->height() : 0;

    if (m_horizontalRuler) {
        m_horizontalRuler->setVisible(m_showDocumentRulers);
        m_horizontalRuler->setGeometry(rulerLeft, 0,
                                       qMax(0, width() - rulerLeft - vScrollbarWidth),
                                       rulerTop);
        m_horizontalRuler->setScrollOffset(horizontalPixelOffset());
    }

    if (m_verticalRuler) {
        m_verticalRuler->setVisible(m_showDocumentRulers);
        m_verticalRuler->setGeometry(0, rulerTop,
                                     rulerLeft,
                                     qMax(0, height() - rulerTop - hScrollbarHeight));
        m_verticalRuler->setScrollOffset(verticalPixelOffset());
    }

    if (m_rulerCorner) {
        m_rulerCorner->setVisible(m_showDocumentRulers);
        m_rulerCorner->setGeometry(0, 0, rulerLeft, rulerTop);
    }

    if (m_crosshairOverlay) {
        m_crosshairOverlay->setVisible(m_showCrosshair);
        m_crosshairOverlay->setGeometry(QRect(QPoint(0, 0), viewport()->size()));
    }

    updateMeasurementTheme();
}

bool Editor::eventFilter(QObject *obj, QEvent *event) {
    if (obj == viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint viewportPos = mouseEvent->pos();
            const QPoint documentPx(horizontalPixelOffset() + viewportPos.x(),
                                    verticalPixelOffset() + viewportPos.y());

            if (m_showDocumentRulers) {
                if (m_horizontalRuler) m_horizontalRuler->setCrosshairPixel(documentPx.x());
                if (m_verticalRuler) m_verticalRuler->setCrosshairPixel(documentPx.y());
            }
            if (m_showCrosshair && m_crosshairOverlay)
                m_crosshairOverlay->setCrosshair(viewportPos, documentPx);
        } else if (event->type() == QEvent::Leave) {
            if (m_horizontalRuler) m_horizontalRuler->clearCrosshairPixel();
            if (m_verticalRuler) m_verticalRuler->clearCrosshairPixel();
            if (m_crosshairOverlay) m_crosshairOverlay->clearCrosshair();
        }
    }
    return QsciScintilla::eventFilter(obj, event);
}

void Editor::resizeEvent(QResizeEvent *event) {
    QsciScintilla::resizeEvent(event);
    syncMeasurementUi();
}

void Editor::onCursorMoved(int line, int col) {
    int pos = static_cast<int>(SendScintilla(SCI_GETCURRENTPOS));
    emit cursorPositionUpdated(line + 1, col + 1, pos);
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
