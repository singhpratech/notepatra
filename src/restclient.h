#ifndef RESTCLIENT_H
#define RESTCLIENT_H

#include <QWidget>
#include <QTextEdit>
#include <QNetworkAccessManager>

class RestClient : public QWidget {
    Q_OBJECT
public:
    explicit RestClient(QWidget *parent = nullptr);
    void executeRequest(const QString &httpText);

private:
    QTextEdit *m_output;
    QNetworkAccessManager *m_nam;
    void parseAndSend(const QString &block);
};

#endif
