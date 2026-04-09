#ifndef SQLFMTPANEL_H
#define SQLFMTPANEL_H

#include <QWidget>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QComboBox>
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
    QComboBox *m_dialectCombo;
    QLabel *m_statusLabel;
    QString m_inputText;
    void doFormat();
    void setStatus(const QString &text, bool error = false);
    void applySqlDialectKeywords();
};

#endif
