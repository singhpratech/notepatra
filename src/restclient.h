#ifndef RESTCLIENT_H
#define RESTCLIENT_H

#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>

class RestClient : public QWidget {
    Q_OBJECT
public:
    explicit RestClient(QWidget *parent = nullptr);
    void executeRequest(const QString &httpText);

private:
    // Top request bar
    QComboBox      *m_methodCombo;
    QLineEdit      *m_urlInput;
    QPushButton    *m_sendBtn;

    // Headers / Body tabs
    QTabWidget     *m_reqTabs;
    QPlainTextEdit *m_headersInput;
    QPlainTextEdit *m_bodyInput;

    // Response panel
    QLabel         *m_statusBadge;  // "200 OK · 43 ms"
    QTextEdit      *m_output;

    QNetworkAccessManager *m_nam;

    // Cached theme colors (captured once in constructor from npPalette())
    // so sendFromUi() can color-code HTTP status badges without calling
    // npPalette() again. 2xx → successFg, 3xx → accent, 4xx → warningFg,
    // 5xx → errorFg.
    QString m_palBg;
    QString m_palChromeBg;
    QString m_palText;
    QString m_palTextMuted;
    QString m_palAccent;
    QString m_palBorder;
    QString m_palSuccessFg;
    QString m_palWarningFg;
    QString m_palErrorFg;

    void sendFromUi();
    void parseAndSend(const QString &block);
};

#endif
