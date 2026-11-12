// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef RESTCLIENT_H
#define RESTCLIENT_H

#include "http_file.h"

#include <QByteArray>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>

class QNetworkReply;
class QSplitter;
class QStackedWidget;
class QTabBar;
class QTreeWidget;

// REST client panel: one request at a time, filled by hand, from a .http
// file (Send Request in the editor), from a pasted curl command, or from
// history. Variables are expanded only at send time; an undefined one
// blocks the send. Redirects are shown, never followed, so credentials
// cannot travel to another host. TLS errors are never ignored.
class RestClient : public QWidget {
    Q_OBJECT
    friend class RestClientTestAccess;

public:
    explicit RestClient(QWidget *parent = nullptr);
    ~RestClient() override;

    // Runs the first request in `httpText` (e.g. an editor selection).
    void executeRequest(const QString &httpText, const QString &filePath = QString());
    // Runs the request whose block contains 0-based `line` of a .http file.
    // Returns false (with a note in the panel) when there is no request there.
    bool sendHttpRequest(const QString &fileText, int line, const QString &filePath);

    bool importCurl(const QString &command);
    QString currentAsCurl() const;
    bool isBusy() const { return !m_reply.isNull(); }

    static bool isBinaryResponse(const QString &contentType, const QByteArray &body);
    static QString languageForContentType(const QString &contentType);
    static QString suggestedFileName(const QString &url, const QString &contentType);

    static constexpr int kRenderCap = 2 * 1024 * 1024;
    static constexpr qint64 kMaxResponse = 256LL * 1024 * 1024;

public slots:
    // Re-applies every palette-dependent stylesheet when MainWindow emits
    // themeChanged().
    void onThemeChanged();
    void send();
    void cancelRequest();

signals:
    // "Open in tab": MainWindow puts `text` in a new untitled editor tab.
    void openInTabRequested(const QString &text, const QString &language);
    void requestFinished();

private:
    struct Context {
        QString filePath;
        QString dir;
        QList<QPair<QString, QString>> fileVariables;
        QString requestName;
    };

    void applyPalette();
    // role: "muted", "success", "accent", "warning" or "error"
    void setBadge(const QString &text, const QString &role);
    void setNote(const QString &text, bool warning = false);
    void refuse(const QString &badge, const QString &detail);
    void fillFromRequest(const QString &method, const QString &url, const QString &headers,
                         const QString &body);
    void setContext(const Context &ctx);
    void reloadEnvironments();
    void onEnvironmentChosen(int index);
    void onReplyFinished(QNetworkReply *reply);
    void renderResponse();
    void showResponseView(int which);
    void updateResponseActions();
    void openImportDialog();
    void copyAsCurl();
    void copyResponse();
    void openResponseInTab();
    void saveResponse();
    bool saveResponseTo(const QString &path, QString *error);
    void reloadHistory();
    void loadHistoryEntry(int row);
    void clearHistory();
    QString historyPath() const;

    // Chrome
    QWidget *m_headerHost = nullptr;
    QLabel *m_header = nullptr;
    QLabel *m_envLabel = nullptr;
    QComboBox *m_envCombo = nullptr;
    QWidget *m_reqBarHost = nullptr;
    QComboBox *m_methodCombo = nullptr;
    QLineEdit *m_urlInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;
    QTabWidget *m_reqTabs = nullptr;
    QPlainTextEdit *m_headersInput = nullptr;
    QPlainTextEdit *m_bodyInput = nullptr;
    QPushButton *m_importBtn = nullptr;
    QPushButton *m_copyCurlBtn = nullptr;
    QLabel *m_statusBadge = nullptr;
    QLabel *m_note = nullptr;
    QWidget *m_respBarHost = nullptr;
    QTabBar *m_respTabs = nullptr;
    QPushButton *m_openTabBtn = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QSplitter *m_split = nullptr;
    QStackedWidget *m_respStack = nullptr;
    QPlainTextEdit *m_respBody = nullptr;
    QPlainTextEdit *m_respHeaders = nullptr;
    QWidget *m_historyHost = nullptr;
    QLabel *m_historyTitle = nullptr;
    QLabel *m_historyHint = nullptr;
    QPushButton *m_clearHistoryBtn = nullptr;
    QTreeWidget *m_history = nullptr;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_reply;
    QElapsedTimer m_elapsed;
    QTimer m_timeout;
    int m_timeoutMs = 30 * 1000;
    bool m_timedOut = false;
    bool m_tooLarge = false;
    QStringList m_sslErrors;

    Context m_ctx;
    HttpFile::Environments m_envs;
    bool m_fillingEnvCombo = false;
    QString m_historyPathOverride;

    // Last response
    QByteArray m_respBytes;
    QString m_respContentType;
    QString m_respUrl;
    bool m_respBinary = false;
    bool m_haveResponse = false;
    bool m_noteWarning = false;
    QString m_badgeRole = QStringLiteral("muted");

    // Request being sent, as typed (unexpanded) for the history entry.
    QString m_sentMethod, m_sentUrl, m_sentHeaders, m_sentBody;
};

#endif
