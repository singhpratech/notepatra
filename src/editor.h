// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EDITOR_H
#define EDITOR_H

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>
#include <QString>
#include <QSet>

class QContextMenuEvent;

class Editor : public QsciScintilla {
    Q_OBJECT
public:
    explicit Editor(QWidget *parent = nullptr);

    bool loadFile(const QString &path, QString *errorOut = nullptr);
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

    // RAII guard for wholesale programmatic setText() outside loadFile().
    // Suppresses per-line change-history churn; on scope exit resets the
    // changed-lines state so markers/counts never reflect a bulk replace.
    class ScopedBulkLoad {
    public:
        explicit ScopedBulkLoad(Editor *e) : m_e(e), m_prev(e->m_loadingFile) {
            e->m_loadingFile = true;
        }
        ~ScopedBulkLoad() {
            m_e->m_loadingFile = m_prev;
            m_e->m_modifiedLines.clear();
            m_e->m_savedLines.clear();
            m_e->markerDeleteAll(22);
            m_e->markerDeleteAll(23);
            emit m_e->changeHistoryUpdated();
        }
        ScopedBulkLoad(const ScopedBulkLoad &) = delete;
        ScopedBulkLoad &operator=(const ScopedBulkLoad &) = delete;
    private:
        Editor *m_e;
        bool m_prev;
    };

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

    // v0.1.45 — explicit Comment / Uncomment per kind, in addition to
    // the toggles. NPP-style: callers know exactly which direction
    // they want, so the right-click menu and Ctrl+K (comment) /
    // Ctrl+Alt+U (uncomment) shortcuts can do "always-add" or
    // "always-remove" without depending on the current line state.
    void commentLine();
    void uncommentLine();
    void commentBlock();
    void uncommentBlock();

    void toggleWordWrap();
    void toggleWhitespace();
    void toggleEol();
    void goToMatchingBrace();
    void clearBraceHighlight();

    // D3 — status-bar word count cache. lastWordCount() is O(1) and may be
    // stale; -1 = not yet computed OR suppressed above kWordCountMaxBytes.
    int lastWordCount() const { return m_wordCount; }
    bool wordCountDirty() const { return m_wordCountDirty; }
    int recomputeWordCount();
    static constexpr int kWordCountMaxBytes = 2 * 1024 * 1024;

    // D8 — session-autosave change detection: set on textChanged, cleared by
    // MainWindow::saveSession after a successful session.json write.
    bool sessionTextDirty() const { return m_sessionTextDirty; }
    void clearSessionTextDirty() { m_sessionTextDirty = false; }

    // Public so the perf contract test can drive it directly.
    void highlightAllOccurrences(const QString &word);

signals:
    void cursorPositionUpdated(int line, int col, int pos);
    void encodingChanged(const QString &name);
    void eolModeChanged(const QString &name);
    // v0.1.92 — emitted whenever m_modifiedLines / m_savedLines changes so
    // the status bar can refresh its "N unsaved · M saved" counter.
    void changeHistoryUpdated();

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onCursorMoved(int line, int col);
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers state);
    // v0.1.91 — manual change-history (Notepad++ style strip in margin 3).
    // The Ubuntu QScintilla 2.14.1 build has Scintilla compiled without
    // SCI_SETCHANGEHISTORY support (the message round-trips as a no-op), so
    // we track per-line modification state ourselves via Scintilla's raw
    // SCN_MODIFIED / SCN_SAVEPOINTREACHED / SCN_SAVEPOINTLEFT signals.
    void onScintillaModified(int position, int modificationType,
                              const char *text, int length, int linesAdded,
                              int line, int foldLevelNow, int foldLevelPrev,
                              int token, int annotationLinesAdded);
    void onSavePointReached();
    void onSavePointLeft();

private:
    void setupEditor();
    void setupMargins();
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
    // Change-history bookkeeping. m_loadingFile gates SCN_MODIFIED so the
    // wholesale setText() during loadFile() doesn't mark every line orange.
    bool m_loadingFile = false;
    // Lines edited since the last save point. Used to flip marker 23 →
    // marker 22 on SCN_SAVEPOINTREACHED.
    QSet<int> m_modifiedLines;
    // Lines that have a green (saved-after-edit) marker. Kept parallel to
    // marker 22 so the status bar can report the saved-line count in O(1)
    // instead of walking the buffer. Cleared on loadFile/setText reload.
    QSet<int> m_savedLines;
    int m_wordCount = -1;
    bool m_wordCountDirty = true;
    bool m_sessionTextDirty = true;  // starts true: first save must serialize

public:
    int modifiedLineCount() const { return m_modifiedLines.size(); }
    int savedLineCount() const { return m_savedLines.size(); }

private:
    bool m_showDocumentRulers = false;
    bool m_showCrosshair = false;
    class EditorRulerBand *m_horizontalRuler = nullptr;
    class EditorRulerBand *m_verticalRuler = nullptr;
    class EditorCrosshairOverlay *m_crosshairOverlay = nullptr;
    QWidget *m_rulerCorner = nullptr;
};

#endif // EDITOR_H
