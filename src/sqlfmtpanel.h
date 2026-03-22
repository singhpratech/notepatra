#ifndef SQLFMTPANEL_H
#define SQLFMTPANEL_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <Qsci/qsciscintilla.h>

class SqlFmtPanel : public QWidget {
    Q_OBJECT
public:
    explicit SqlFmtPanel(QWidget *parent = nullptr);
    void setInput(const QString &sql);

signals:
    void applyFormatted(const QString &text);

private:
    QsciScintilla *m_output;
    QCheckBox *m_uppercase;
    QSpinBox *m_indent;
    QString m_inputText;
    void doFormat();
};

#endif
