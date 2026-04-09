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

    QString filePath() const { return m_filePath; }
    QString language() const { return m_language; }
    QString encoding() const { return m_encoding; }
    QString eolModeName() const { return m_eolName; }

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
    void cursorPositionUpdated(int line, int col);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private slots:
    void onCursorMoved(int line, int col);
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers state);

private:
    void setupEditor();
    void setupMargins();
    void highlightAllOccurrences(const QString &word);
    void applyLexer(const QString &lang);
    void applySyntaxColors(QsciLexer *lexer, const QString &themeName);
    void applyNotepadPlusPalette(QsciLexer *lexer, const QFont &baseFont);

    QString m_filePath;
    QString m_language = "Plain Text";
    QString m_encoding = "UTF-8";
    QString m_eolName = "Unix (LF)";
    QString m_themeName;
};

#endif // EDITOR_H
