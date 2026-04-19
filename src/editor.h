#ifndef EDITOR_H
#define EDITOR_H

#include <Qsci/qsciscintilla.h>
#include <Qsci/qscilexer.h>
#include <QString>

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

    QString filePath() const { return m_filePath; }
    QString language() const { return m_language; }
    QString encoding() const { return m_encoding; }
    QString eolModeName() const { return m_eolName; }

    // Public so tests can verify the exact Notepad++ palette is applied
    // without having to construct a full Editor (which pulls in the Rust core).
    static void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont);

    void gotoLine(int line);
    void updateGitGutter();
    void duplicateLine();
    void deleteLine();
    void moveLineUp();
    void moveLineDown();
    void toggleComment();
    void toggleWordWrap();
    void toggleWhitespace();
    void toggleEol();
    void goToMatchingBrace();
    void clearBraceHighlight();

signals:
    void cursorPositionUpdated(int line, int col, int pos);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
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
