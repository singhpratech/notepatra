#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <QWidget>
#include <QLabel>

class NppStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit NppStatusBar(QWidget *parent = nullptr);

    void updatePosition(int line, int col);
    void updateSelection(int chars, int lines);
    void updateLines(int count);
    void updateLength(int length);
    void updateWords(int count);
    void updateLanguage(const QString &lang);
    void updateEncoding(const QString &enc);
    void updateEol(const QString &eol);
    void updateInsertMode(bool overwrite);
    void applyColors(const QString &bg, const QString &fg, const QString &sep);

private:
    QLabel *m_lang, *m_size, *m_pos, *m_eol, *m_enc, *m_ins;
};

#endif
