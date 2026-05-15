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

#include <QAction>
#include <QContextMenuEvent>
#include <QFile>
#include <QFont>
#include <QColor>
#include <QFileInfo>
#include <QHash>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTextCodec>
#include <QToolTip>
#include "gitgutter.h"
#include "git_hunk_apply.h"
#include "gutter_hunk_popup.h"

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

    // v0.1.56 — multi-cursor / column-edit support.
    // SCI_SETMULTIPLESELECTION              → allow more than one caret/selection
    // SCI_SETADDITIONALSELECTIONTYPING      → typing inserts at every caret
    // SCI_SETMULTIPASTE                     → paste duplicates to every selection
    // SCI_SETADDITIONALCARETSBLINK / VISIBLE→ secondary carets render exactly like
    //                                          the primary so the user sees them all
    // SCI_SETRECTANGULARSELECTIONMODIFIER   → Alt+drag → column / rectangular select
    //                                          (matches VS Code, Sublime, Notepad++)
    // After this is enabled the user can:
    //   • Ctrl+click anywhere to ADD a caret at that position
    //   • Alt+drag down 50 lines to make a column selection
    //   • Type once and the same text appears at every caret / column row
    //   • Esc collapses back to a single caret
    SendScintilla(SCI_SETMULTIPLESELECTION, 1);
    SendScintilla(SCI_SETADDITIONALSELECTIONTYPING, 1);
    SendScintilla(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    SendScintilla(SCI_SETADDITIONALCARETSBLINK, 1);
    SendScintilla(SCI_SETADDITIONALCARETSVISIBLE, 1);
    SendScintilla(SCI_SETRECTANGULARSELECTIONMODIFIER, SCMOD_ALT);
    // Make additional selections visually distinct enough to see at a glance.
    SendScintilla(SCI_SETADDITIONALSELALPHA, 90);

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
    QString finalLabel = name;
    if (name.compare("UTF-8 BOM", Qt::CaseInsensitive) == 0 ||
        name.compare("UTF-8-BOM", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("EFBBBF")))
            bytes.remove(0, 3);
        decoded = QString::fromUtf8(bytes);
        finalLabel = QStringLiteral("UTF-8 BOM");
    } else if (name.compare("UTF-8", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("EFBBBF")))
            bytes.remove(0, 3);
        decoded = QString::fromUtf8(bytes);
    } else if (name.compare("UTF-16 LE BOM", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("FFFE")))
            bytes.remove(0, 2);
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        if (!codec) return false;
        decoded = codec->toUnicode(bytes);
    } else if (name.compare("UTF-16 BE BOM", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("FEFF")))
            bytes.remove(0, 2);
        QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");
        if (!codec) return false;
        decoded = codec->toUnicode(bytes);
    } else if (name.compare("UTF-32 LE BOM", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("FFFE0000")))
            bytes.remove(0, 4);
        const int n = bytes.size() / 4;
        decoded.reserve(n);
        const uchar *p = reinterpret_cast<const uchar*>(bytes.constData());
        for (int i = 0; i < n; ++i) {
            const uchar *q = p + i * 4;
            const quint32 cp = quint32(q[0]) | (quint32(q[1]) << 8) |
                               (quint32(q[2]) << 16) | (quint32(q[3]) << 24);
            decoded.append(QChar(static_cast<int>(cp)));
        }
    } else if (name.compare("UTF-32 BE BOM", Qt::CaseInsensitive) == 0) {
        if (bytes.startsWith(QByteArray::fromHex("0000FEFF")))
            bytes.remove(0, 4);
        const int n = bytes.size() / 4;
        decoded.reserve(n);
        const uchar *p = reinterpret_cast<const uchar*>(bytes.constData());
        for (int i = 0; i < n; ++i) {
            const uchar *q = p + i * 4;
            const quint32 cp = (quint32(q[0]) << 24) | (quint32(q[1]) << 16) |
                               (quint32(q[2]) << 8) | quint32(q[3]);
            decoded.append(QChar(static_cast<int>(cp)));
        }
    } else {
        QTextCodec *codec = QTextCodec::codecForName(name.toUtf8());
        if (!codec) {
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
    m_encoding = finalLabel;
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
    // v0.1.62 — margin is now sensitive so QScintilla emits marginClicked
    // for the green / yellow / red bars. The handler in onMarginClicked
    // looks up the containing hunk via GitGutter::hunksForFile and pops
    // the GutterHunkPopup at the clicked line.
    setMarginSensitivity(3, true);
    // Marker 1 = git added (green bar)
    markerDefine(QsciScintilla::Background, 1);
    setMarkerBackgroundColor(QColor("#4CAF50"), 1);
    // Marker 2 = git modified (yellow bar)
    markerDefine(QsciScintilla::Background, 2);
    setMarkerBackgroundColor(QColor("#FFC107"), 2);
    // Marker 3 = git deleted (red bar)
    markerDefine(QsciScintilla::Background, 3);
    setMarkerBackgroundColor(QColor("#F44336"), 3);

    // Setup indicator 9 for double-click word highlight — NEON orange
    // (#FF5500) with high alpha so matches pop. Pure-saturation orange
    // reads as a true highlight, not a wash.
    //
    // Scintilla's SCI_INDICSETFORE expects a Win32 COLORREF (0x00BBGGRR),
    // NOT Qt's RGB packing. Always BGR-pack.
    SendScintilla(SCI_INDICSETSTYLE, 9, INDIC_ROUNDBOX);
    {
        QColor hi("#FF5500");
        long bgr = (long(hi.blue()) << 16) | (long(hi.green()) << 8) | long(hi.red());
        SendScintilla(SCI_INDICSETFORE, 9, bgr);
    }
    SendScintilla(SCI_INDICSETALPHA, 9, 160);
    SendScintilla(SCI_INDICSETOUTLINEALPHA, 9, 255);
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
        // v0.1.87 — extra UX gates for large files. Word wrap recalculates
        // line layout on every edit; on a 118 MB file this stutters the
        // typing cursor. Disabling it brings editing back to instant. User
        // can re-enable from View menu if they need it. Tracked as part of
        // the "Up to 2 GB" file-size guarantee — without these gates, big
        // files technically opened but were sluggish to edit.
        setWrapMode(QsciScintilla::WrapNone);
        // Indent guides + edge column don't add value on plain-text dumps
        // and they trigger a paint per line on resize.
        setIndentationGuides(false);
        setEdgeMode(QsciScintilla::EdgeNone);
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

    // v0.1.78 — full BOM round-trip. Pre-v0.1.78, opening a UTF-16 LE BOM
    // file (e.g. SQL Server Generate-Scripts output) and saving it would
    // silently drop the BOM because QTextCodec("UTF-16LE") emits no BOM.
    // Now we prepend the BOM manually when the label says BOM, so the
    // file round-trips byte-for-byte the way Notepad++ does.
    const QString lower = enc.toLower();
    if (enc.compare("UTF-8 BOM", Qt::CaseInsensitive) == 0 ||
        enc.compare("UTF-8-BOM", Qt::CaseInsensitive) == 0) {
        bytes = QByteArray::fromHex("EFBBBF");
        bytes += textOut.toUtf8();
    } else if (enc.compare("UTF-16 LE BOM", Qt::CaseInsensitive) == 0) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        bytes = QByteArray::fromHex("FFFE");
        if (codec) bytes += codec->fromUnicode(textOut);
    } else if (enc.compare("UTF-16 BE BOM", Qt::CaseInsensitive) == 0) {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");
        bytes = QByteArray::fromHex("FEFF");
        if (codec) bytes += codec->fromUnicode(textOut);
    } else if (enc.compare("UTF-32 LE BOM", Qt::CaseInsensitive) == 0) {
        bytes = QByteArray::fromHex("FFFE0000");
        for (const QChar c : textOut) {
            const quint32 cp = c.unicode();
            bytes.append(char(cp & 0xFF));
            bytes.append(char((cp >> 8) & 0xFF));
            bytes.append(char((cp >> 16) & 0xFF));
            bytes.append(char((cp >> 24) & 0xFF));
        }
    } else if (enc.compare("UTF-32 BE BOM", Qt::CaseInsensitive) == 0) {
        bytes = QByteArray::fromHex("0000FEFF");
        for (const QChar c : textOut) {
            const quint32 cp = c.unicode();
            bytes.append(char((cp >> 24) & 0xFF));
            bytes.append(char((cp >> 16) & 0xFF));
            bytes.append(char((cp >> 8) & 0xFF));
            bytes.append(char(cp & 0xFF));
        }
    } else if (enc.compare("UTF-8", Qt::CaseInsensitive) == 0 || enc.isEmpty()) {
        bytes = textOut.toUtf8();
    } else {
        // Pick the right codec by name, with friendly aliases.
        QTextCodec *codec = QTextCodec::codecForName(enc.toUtf8());
        if (!codec) {
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

    // v0.1.88.1 — if the save target's extension implies a different
    // language than the editor's current one (Save As from .txt → .cpp,
    // or from .py → .rs), re-apply the lexer so syntax highlighting +
    // status bar language indicator reflect the new file type live.
    // Pre-fix: post-Save-As the editor kept the old language indefinitely
    // until the user re-opened the file. User-reported.
    const bool pathChanged = (m_filePath != savePath);
    m_filePath = savePath;
    if (pathChanged) {
        const QString newLang = detectLanguageFromPath(savePath, textOut);
        if (!newLang.isEmpty() && newLang != m_language) {
            applyLexer(newLang);
        }
    }
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
        // v0.1.84 — push curated SCI_SETKEYWORDS strings (sql_keywords.h /
        // lang_keywords.h) into Scintilla for this language. Closes the gap
        // where MERGE, RETURNING, LATERAL, JSONB, ... rendered as plain
        // identifier text because the lexer-bundled keyword set was stale.
        // Must run BEFORE applyNotepadPlusPalette so the palette paints the
        // freshly registered keyword tokens.
        populateExtraKeywords(this, QString::fromLatin1(
            lexer->language() ? lexer->language() : ""));
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
    } else if (margin == 3) {
        // v0.1.62 — Git gutter click. Find the hunk that contains the
        // clicked (0-based) line, build a GutterHunkPopup, and anchor it
        // to that line's screen coordinates. Bails silently if the file
        // isn't on disk, isn't in a git repo, or has no hunk at this
        // line (e.g. user clicked the gutter between two hunks).
        if (m_filePath.isEmpty()) return;
        const int oneBased = line + 1;

        const QVector<DiffHunk> hunks = GitGutter::hunksForFile(m_filePath);
        if (hunks.isEmpty()) return;

        const int idx = GitGutter::hunkIndexForLine(hunks, oneBased);
        if (idx < 0) return;
        const DiffHunk &hunk = hunks[idx];

        // Reconstruct before/after text for the popup's DiffView. The
        // "before" is the slice of HEAD that the hunk covers; the
        // "after" is the slice of the on-disk file that the hunk
        // produces. We rebuild both directly from the hunk body so the
        // preview matches exactly the lines the user would stage.
        QStringList beforeLines, afterLines;
        for (const QString &raw : hunk.rawDiffLines) {
            if (raw.isEmpty()) continue;
            const QChar c = raw[0];
            const QString rest = raw.mid(1);
            if (c == '-') {
                beforeLines.append(rest);
            } else if (c == '+') {
                afterLines.append(rest);
            } else if (c == ' ') {
                beforeLines.append(rest);
                afterLines.append(rest);
            }
            // '\' = "No newline at end of file" — visually irrelevant.
        }
        const QString beforeText = beforeLines.join('\n');
        const QString afterText  = afterLines.join('\n');

        // Resolve the repo root from the file path itself — we can't
        // assume MainWindow set a workspace, the file may be opened
        // ad-hoc.
        const QString repoRoot = GitHunkApply::repoRootForFile(m_filePath);
        if (repoRoot.isEmpty()) return;

        // Anchor: convert the (line, 0) caret position to a viewport
        // pixel, then to global. QScintilla exposes SCI_POINTXFROMPOSITION
        // and SCI_POINTYFROMPOSITION for that purpose. We add a small
        // x-offset so the popup sits to the RIGHT of the gutter rather
        // than over the gutter itself.
        const int pos = positionFromLineIndex(line, 0);
        const int px = (int)SendScintilla(SCI_POINTXFROMPOSITION, 0L, (long)pos);
        const int py = (int)SendScintilla(SCI_POINTYFROMPOSITION, 0L, (long)pos);
        const QPoint local(px + 8, py);
        const QPoint globalPos = viewport()->mapToGlobal(local);

        auto *popup = new GutterHunkPopup(m_filePath, repoRoot, hunk,
                                          beforeText, afterText, this);
        // Refresh the gutter as soon as the index changes — the marker
        // disappears the moment staging succeeds.
        connect(popup, &GutterHunkPopup::hunkStaged, this, [this](const QString &) {
            updateGitGutter();
        });
        // Reverts mutate the working copy itself; we need to re-read the
        // file from disk so the buffer matches.
        connect(popup, &GutterHunkPopup::hunkReverted, this, [this](const QString &path) {
            // Only reload if the buffer is clean — we don't want to clobber
            // unsaved edits. (The popup only operates on the on-disk diff,
            // but the buffer may have post-save edits on top.)
            if (!isModified()) loadFile(path);
            updateGitGutter();
        });
        popup->showAt(globalPos);
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

// v0.1.44 — single source of truth for line + block comment syntax per
// language. Returns empty strings for languages that don't have that
// kind of comment; callers (toggleComment, toggleBlockComment, the
// right-click menu) check emptiness to disable the action.
Editor::CommentSyntax Editor::commentSyntaxFor(const QString &lang) {
    static const QHash<QString, CommentSyntax> map = {
        // Hash-comment family
        {"Python",       {"#",     "",        ""      }},
        {"Bash",         {"#",     "",        ""      }},
        {"YAML",         {"#",     "",        ""      }},
        {"Ruby",         {"#",     "=begin",  "=end"  }},
        {"Perl",         {"#",     "",        ""      }},
        {"PowerShell",   {"#",     "<#",      "#>"    }},
        {"TCL",          {"#",     "",        ""      }},
        {"CMake",        {"#",     "",        ""      }},
        {"Makefile",     {"#",     "",        ""      }},
        {"Properties",   {"#",     "",        ""      }},
        {"AVS",          {"#",     "",        ""      }},
        // C-family (// line + /* */ block)
        {"C",            {"//",    "/*",      "*/"    }},
        {"C++",          {"//",    "/*",      "*/"    }},
        {"C#",           {"//",    "/*",      "*/"    }},
        {"Java",         {"//",    "/*",      "*/"    }},
        {"JavaScript",   {"//",    "/*",      "*/"    }},
        {"TypeScript",   {"//",    "/*",      "*/"    }},
        {"D",            {"//",    "/*",      "*/"    }},
        {"Rust",         {"//",    "/*",      "*/"    }},
        {"Go",           {"//",    "/*",      "*/"    }},
        {"Swift",        {"//",    "/*",      "*/"    }},
        {"Kotlin",       {"//",    "/*",      "*/"    }},
        {"CSS",          {"",      "/*",      "*/"    }},
        {"Verilog",      {"//",    "/*",      "*/"    }},
        {"Pascal",       {"//",    "{",       "}"     }},
        {"POV",          {"//",    "/*",      "*/"    }},
        // SQL-family (-- line + /* */ block)
        {"SQL",          {"--",    "/*",      "*/"    }},
        {"VHDL",         {"--",    "",        ""      }},
        // Lua
        {"Lua",          {"--",    "--[[",    "]]"    }},
        // Markup (no line comment, only block)
        {"HTML",         {"",      "<!--",    "-->"   }},
        {"XML",          {"",      "<!--",    "-->"   }},
        {"Markdown",     {"",      "<!--",    "-->"   }},
        // Assembly / scripting variants
        {"ASM",          {";",     "",        ""      }},
        {"NASM",         {";",     "",        ""      }},
        {"MASM",         {";",     "",        ""      }},
        {"IDL",          {";",     "",        ""      }},
        {"Spice",        {";",     "",        ""      }},
        {"PostScript",   {"%",     "",        ""      }},
        {"TeX",          {"%",     "",        ""      }},
        {"Matlab",       {"%",     "%{",      "%}"    }},
        {"Octave",       {"%",     "%{",      "%}"    }},
        {"Fortran",      {"!",     "",        ""      }},
        {"Fortran77",    {"!",     "",        ""      }},
        {"CoffeeScript", {"#",     "###",     "###"   }},
        {"Batch",        {"REM ",  "",        ""      }},
        // Explicitly no comment syntax — menu items will be disabled.
        // Listed so the lookup is exhaustive and adding a new lang
        // surfaces here rather than silently falling through to
        // an empty default.
        {"JSON",         {"",      "",        ""      }},
        {"Diff",         {"",      "",        ""      }},
        {"IntelHex",     {"",      "",        ""      }},
        {"SRecord",      {"",      "",        ""      }},
        {"Plain Text",   {"",      "",        ""      }},
        // v0.1.55 — comment syntax for the 32 new lexers shipped in this
        // release. Each entry is sourced from the language's official
        // reference docs (linked in src/lexer_extras.cpp). Without this
        // map populated, Ctrl+Q (Toggle Line Comment) and Ctrl+Shift+Q
        // (Toggle Block Comment) would no-op for the new languages.
        // C-family — // line + /* */ block
        {"Dart",         {"//",    "/*",      "*/"    }},
        {"Solidity",     {"//",    "/*",      "*/"    }},
        {"Zig",          {"//",    "",        ""      }},  // no block comments by design
        {"Vala",         {"//",    "/*",      "*/"    }},
        {"Hack",         {"//",    "/*",      "*/"    }},
        {"Protobuf",     {"//",    "/*",      "*/"    }},
        {"Thrift",       {"//",    "/*",      "*/"    }},  // Thrift also accepts # but // is canonical
        {"GraphQL",      {"#",     "",        ""      }},  // GraphQL spec uses # only
        {"Scala",        {"//",    "/*",      "*/"    }},
        {"Groovy",       {"//",    "/*",      "*/"    }},
        {"Apex",         {"//",    "/*",      "*/"    }},
        // R, Julia — # line + #= =# block (Julia)
        {"R",            {"#",     "",        ""      }},
        {"Julia",        {"#",     "#=",      "=#"    }},
        // F# — // line + (* *) block, ML-derived
        {"F#",           {"//",    "(*",      "*)"    }},
        // HCL / Terraform — # OR // line, /* */ block
        {"HCL",          {"#",     "/*",      "*/"    }},
        // Python-family — # line, no block (use ''' or """)
        {"GDScript",     {"#",     "",        ""      }},
        {"Nim",          {"#",     "#[",      "]#"    }},
        {"Cython",       {"#",     "",        ""      }},
        {"Mojo",         {"#",     "",        ""      }},
        // Ruby-family
        {"Crystal",      {"#",     "",        ""      }},
        // Elixir — # line, no formal block; convention is #'#'#
        {"Elixir",       {"#",     "",        ""      }},
        // HTML-templating engines — Jinja {# #}, Liquid {% comment %},
        // Twig {# #} (host HTML uses <!-- --> separately).
        {"Jinja",        {"",      "{#",      "#}"    }},
        {"Liquid",       {"",      "{% comment %}", "{% endcomment %}"}},
        {"Twig",         {"",      "{#",      "#}"    }},
        // Shell-family — # line. Nushell + Fish + Dockerfile all use #
        {"Dockerfile",   {"#",     "",        ""      }},
        {"Fish",         {"#",     "",        ""      }},
        {"Nushell",      {"#",     "",        ""      }},
        // Properties / config formats
        {"TOML",         {"#",     "",        ""      }},
        {"DotEnv",       {"#",     "",        ""      }},
        {"Gitignore",    {"#",     "",        ""      }},
        // JSON5 (extends JSON which has no comments — JSON5 adds them)
        {"JSON5",        {"//",    "/*",      "*/"    }},
        // BibTeX — % line comment per BibTeX convention; @comment{...}
        // is a directive, not a comment per se.
        {"BibTeX",       {"%",     "",        ""      }},
    };
    return map.value(lang, CommentSyntax{"", "", ""});
}

void Editor::toggleComment() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.line.isEmpty()) return;
    const QString comment = cs.line;

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

void Editor::toggleBlockComment() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.blockOpen.isEmpty() || cs.blockClose.isEmpty()) return;

    int lineFrom = 0, lineTo = 0, colFrom = 0, colTo = 0;
    bool hadSelection = hasSelectedText();
    if (hadSelection) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
    } else {
        // No selection → wrap the current line.
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
        colFrom = 0;
        colTo = text(lineFrom).length();
    }

    QString sel;
    if (hadSelection) {
        sel = selectedText();
    } else {
        sel = text(lineFrom);
        if (sel.endsWith('\n')) sel.chop(1);
    }

    beginUndoAction();
    const QString trimmed = sel.trimmed();
    if (trimmed.startsWith(cs.blockOpen) && trimmed.endsWith(cs.blockClose) &&
        trimmed.length() >= cs.blockOpen.length() + cs.blockClose.length()) {
        // Already wrapped — strip the markers.
        const int openIdx  = sel.indexOf(cs.blockOpen);
        const int closeIdx = sel.lastIndexOf(cs.blockClose);
        if (openIdx >= 0 && closeIdx > openIdx) {
            QString inner = sel.mid(openIdx + cs.blockOpen.length(),
                                    closeIdx - openIdx - cs.blockOpen.length());
            // Strip a single leading/trailing space that toggleBlockComment
            // itself inserted on the wrap step, so a wrap+unwrap round-trip
            // is idempotent.
            if (inner.startsWith(' ')) inner.remove(0, 1);
            if (inner.endsWith(' ')) inner.chop(1);
            QString prefix = sel.left(openIdx);
            QString suffix = sel.mid(closeIdx + cs.blockClose.length());
            QString result = prefix + inner + suffix;
            if (hadSelection) {
                setSelection(lineFrom, colFrom, lineTo, colTo);
                replaceSelectedText(result);
            } else {
                setSelection(lineFrom, 0, lineFrom, text(lineFrom).length());
                replaceSelectedText(result);
            }
        }
    } else {
        const QString wrapped = cs.blockOpen + " " + sel + " " + cs.blockClose;
        if (hadSelection) {
            replaceSelectedText(wrapped);
        } else {
            setSelection(lineFrom, 0, lineFrom, text(lineFrom).length());
            replaceSelectedText(wrapped);
        }
    }
    endUndoAction();
}

// v0.1.45 — explicit Comment-only / Uncomment-only operations. The
// toggle helpers above flip state; these always go in one direction,
// matching Notepad++'s separate menu items + Ctrl+K / Ctrl+Shift+K
// shortcuts.
void Editor::commentLine() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.line.isEmpty()) return;

    int lineFrom, lineTo, colFrom, colTo;
    if (hasSelectedText()) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
        if (colTo == 0) lineTo--;
    } else {
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
    }

    beginUndoAction();
    for (int i = lineFrom; i <= lineTo; i++) {
        // Skip lines that already start with the comment token —
        // commentLine never DOUBLES the comment marker.
        if (text(i).trimmed().startsWith(cs.line)) continue;
        insertAt(cs.line + " ", i, 0);
    }
    endUndoAction();
}

void Editor::uncommentLine() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.line.isEmpty()) return;

    int lineFrom, lineTo, colFrom, colTo;
    if (hasSelectedText()) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
        if (colTo == 0) lineTo--;
    } else {
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
    }

    beginUndoAction();
    for (int i = lineFrom; i <= lineTo; i++) {
        QString line = text(i);
        const int idx = line.indexOf(cs.line);
        // Only strip if the comment is the FIRST non-whitespace token
        // on the line — otherwise we'd remove the `--` from a SQL
        // expression like `WHERE x = a-b -- comment`.
        if (idx < 0) continue;
        if (line.left(idx).trimmed().isEmpty()) {
            int removeLen = cs.line.length();
            if (idx + removeLen < line.length() && line[idx + removeLen] == ' ')
                removeLen++;
            setSelection(i, idx, i, idx + removeLen);
            removeSelectedText();
        }
    }
    endUndoAction();
}

void Editor::commentBlock() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.blockOpen.isEmpty() || cs.blockClose.isEmpty()) return;

    int lineFrom = 0, lineTo = 0, colFrom = 0, colTo = 0;
    bool hadSelection = hasSelectedText();
    if (hadSelection) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
    } else {
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
        colFrom = 0;
        colTo = text(lineFrom).length();
    }

    QString sel;
    if (hadSelection) {
        sel = selectedText();
    } else {
        sel = text(lineFrom);
        if (sel.endsWith('\n')) sel.chop(1);
    }

    // commentBlock is unconditional: always wrap. If the user wants
    // toggle behaviour they can use Ctrl+Shift+Q.
    beginUndoAction();
    const QString wrapped = cs.blockOpen + " " + sel + " " + cs.blockClose;
    if (hadSelection) {
        replaceSelectedText(wrapped);
    } else {
        setSelection(lineFrom, 0, lineFrom, text(lineFrom).length());
        replaceSelectedText(wrapped);
    }
    endUndoAction();
}

void Editor::uncommentBlock() {
    const CommentSyntax cs = commentSyntaxFor(m_language);
    if (cs.blockOpen.isEmpty() || cs.blockClose.isEmpty()) return;

    int lineFrom = 0, lineTo = 0, colFrom = 0, colTo = 0;
    bool hadSelection = hasSelectedText();
    if (hadSelection) {
        getSelection(&lineFrom, &colFrom, &lineTo, &colTo);
    } else {
        getCursorPosition(&lineFrom, &colFrom);
        lineTo = lineFrom;
        colFrom = 0;
        colTo = text(lineFrom).length();
    }

    QString sel;
    if (hadSelection) {
        sel = selectedText();
    } else {
        sel = text(lineFrom);
        if (sel.endsWith('\n')) sel.chop(1);
    }

    const QString trimmed = sel.trimmed();
    if (!(trimmed.startsWith(cs.blockOpen) && trimmed.endsWith(cs.blockClose) &&
          trimmed.length() >= cs.blockOpen.length() + cs.blockClose.length())) {
        return; // not wrapped — uncommentBlock is a no-op (idempotent).
    }

    const int openIdx  = sel.indexOf(cs.blockOpen);
    const int closeIdx = sel.lastIndexOf(cs.blockClose);
    if (openIdx < 0 || closeIdx <= openIdx) return;

    QString inner = sel.mid(openIdx + cs.blockOpen.length(),
                            closeIdx - openIdx - cs.blockOpen.length());
    if (inner.startsWith(' ')) inner.remove(0, 1);
    if (inner.endsWith(' ')) inner.chop(1);
    QString prefix = sel.left(openIdx);
    QString suffix = sel.mid(closeIdx + cs.blockClose.length());
    QString result = prefix + inner + suffix;

    beginUndoAction();
    if (hadSelection) {
        setSelection(lineFrom, colFrom, lineTo, colTo);
        replaceSelectedText(result);
    } else {
        setSelection(lineFrom, 0, lineFrom, text(lineFrom).length());
        replaceSelectedText(result);
    }
    endUndoAction();
}

void Editor::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);

    auto *undoAct = menu.addAction(tr("&Undo"), this, [this]() { undo(); });
    undoAct->setShortcut(QKeySequence::Undo);
    undoAct->setEnabled(isUndoAvailable());

    auto *redoAct = menu.addAction(tr("&Redo"), this, [this]() { redo(); });
    redoAct->setShortcut(QKeySequence::Redo);
    redoAct->setEnabled(isRedoAvailable());

    menu.addSeparator();

    auto *cutAct = menu.addAction(tr("Cu&t"), this, [this]() { cut(); });
    cutAct->setShortcut(QKeySequence::Cut);
    cutAct->setEnabled(hasSelectedText());

    auto *copyAct = menu.addAction(tr("&Copy"), this, [this]() { copy(); });
    copyAct->setShortcut(QKeySequence::Copy);
    copyAct->setEnabled(hasSelectedText());

    auto *pasteAct = menu.addAction(tr("&Paste"), this, [this]() { paste(); });
    pasteAct->setShortcut(QKeySequence::Paste);

    menu.addSeparator();
    menu.addAction(tr("Select &All"), this, [this]() { selectAll(); })
        ->setShortcut(QKeySequence::SelectAll);

    menu.addSeparator();

    // ── v0.1.44 — language-aware comment toggles ──
    const CommentSyntax cs = commentSyntaxFor(m_language);

    auto *lineAct = menu.addAction(
        cs.line.isEmpty()
            ? tr("Toggle &Line Comment  (no syntax for %1)").arg(m_language)
            : tr("Toggle &Line Comment  (%1)").arg(cs.line.trimmed().isEmpty() ? cs.line : cs.line.trimmed()),
        this, [this]() { toggleComment(); });
    lineAct->setShortcut(QKeySequence("Ctrl+Q"));
    lineAct->setEnabled(!cs.line.isEmpty());

    auto *blockAct = menu.addAction(
        (cs.blockOpen.isEmpty() || cs.blockClose.isEmpty())
            ? tr("Toggle &Block Comment  (no syntax for %1)").arg(m_language)
            : tr("Toggle &Block Comment  (%1 %2)").arg(cs.blockOpen, cs.blockClose),
        this, [this]() { toggleBlockComment(); });
    blockAct->setShortcut(QKeySequence("Ctrl+Shift+Q"));
    blockAct->setEnabled(!cs.blockOpen.isEmpty() && !cs.blockClose.isEmpty());

    // ── v0.1.45 — explicit Comment / Uncomment, NPP-style ───────────
    menu.addSeparator();

    auto *cmtLineAct = menu.addAction(tr("&Comment Line"), this,
        [this]() { commentLine(); });
    cmtLineAct->setShortcut(QKeySequence("Ctrl+K"));
    cmtLineAct->setEnabled(!cs.line.isEmpty());

    auto *uncmtLineAct = menu.addAction(tr("&Uncomment Line"), this,
        [this]() { uncommentLine(); });
    uncmtLineAct->setShortcut(QKeySequence("Ctrl+Shift+K"));
    uncmtLineAct->setEnabled(!cs.line.isEmpty());

    auto *cmtBlockAct = menu.addAction(tr("Co&mment Block"), this,
        [this]() { commentBlock(); });
    cmtBlockAct->setEnabled(!cs.blockOpen.isEmpty() && !cs.blockClose.isEmpty());

    auto *uncmtBlockAct = menu.addAction(tr("Un&comment Block"), this,
        [this]() { uncommentBlock(); });
    uncmtBlockAct->setEnabled(!cs.blockOpen.isEmpty() && !cs.blockClose.isEmpty());

    menu.exec(event->globalPos());
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
