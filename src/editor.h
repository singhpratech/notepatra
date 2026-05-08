#ifndef EDITOR_H
#define EDITOR_H

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>
#include <QString>

class QContextMenuEvent;

class Editor : public QsciScintilla {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);

    bool loadFile(const QString &path);
    bool saveFile(const QString &path = QString());
    void setLanguage(const QString &lang);
    void applyTheme(const QString &themeName);
    void setDocumentRulersVisible(bool visible);
    void setCrosshairVisible(bool visible);
    bool documentRulersVisible() const { return m_showDocumentRulers; }
    bool crosshairVisible() const { return m_showCrosshair; }

    // v0.1.42 — re-apply every Config-controlled editor setting to this
    // instance. Used by setupEditor() at construction, by the Preferences
    // dialog after the user clicks OK, and by anything else that mutates
    // the global Config and needs the change to propagate. Single source
    // of truth — every Config field that affects the editor is consumed
    // here, NOT scattered across the codebase.
    void applyConfig();

    // v0.1.42 — encoding API that actually works.
    // setEncoding(name) just updates m_encoding so future saves use it.
    // reloadWithEncoding(name) re-reads bytes from disk and decodes them
    // through the new codec (refuses + returns false if buffer is dirty
    // unless force=true, since the in-memory edits would be lost).
    // convertEncoding(name) keeps current text but changes the encoding
    // label so the next save writes the bytes in the new encoding (and
    // adds/removes BOM as appropriate).
    void setEncoding(const QString &name);
    bool reloadWithEncoding(const QString &name, bool force = false);
    void convertEncoding(const QString &name);

    // v0.1.42 — EOL mode by name. Updates QsciScintilla's eol mode AND
    // m_eolName so the status bar reflects the change. Optionally
    // converts existing line endings via convertEols().
    void setEolModeByName(const QString &name, bool convertExisting);

    // v0.1.42 — zoom helpers that actually persist.
    void zoomInPersistent();
    void zoomOutPersistent();
    void zoomResetPersistent();

    QString filePath() const { return m_filePath; }
    QString language() const { return m_language; }
    QString encoding() const { return m_encoding; }
    QString eolModeName() const { return m_eolName; }

    // Public so tests can verify the exact Notepad++ palette is applied
    // without having to construct a full Editor (which pulls in the Rust core).
    static void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont);

    // v0.1.44 — language-aware comment syntax. Public + static so tests
    // and the right-click menu can both query without constructing an
    // Editor. Returns empty strings for languages that lack the given
    // comment kind; "Plain Text" / unknown returns all empty.
    struct CommentSyntax { QString line; QString blockOpen; QString blockClose; };
    static CommentSyntax commentSyntaxFor(const QString &lang);

    void gotoLine(int line);
    void updateGitGutter();
    void duplicateLine();
    void deleteLine();
    void moveLineUp();
    void moveLineDown();
    void toggleComment();
    void toggleBlockComment();
    void toggleWordWrap();
    void toggleWhitespace();
    void toggleEol();
    void goToMatchingBrace();
    void clearBraceHighlight();

signals:
    void cursorPositionUpdated(int line, int col, int pos);
    void encodingChanged(const QString &name);
    void eolModeChanged(const QString &name);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onCursorMoved(int line, int col);
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers state);

private:
    void setupEditor();
    void setupMargins();
    void highlightAllOccurrences(const QString &word);
    void applyLexer(const QString &lang);
    void applySyntaxColors(QsciLexer *lexer, const QString &themeName);
    void syncMeasurementUi();
    void updateMeasurementTheme();
    int horizontalPixelOffset() const;
    int verticalPixelOffset() const;

    QString m_filePath;
    QString m_language = "Plain Text";
    QString m_encoding = "UTF-8";
    QString m_eolName = "Unix (LF)";
    QString m_themeName;
    bool m_showDocumentRulers = false;
    bool m_showCrosshair = false;
    class EditorRulerBand *m_horizontalRuler = nullptr;
    class EditorRulerBand *m_verticalRuler = nullptr;
    class EditorCrosshairOverlay *m_crosshairOverlay = nullptr;
    QWidget *m_rulerCorner = nullptr;
};

#endif // EDITOR_H
