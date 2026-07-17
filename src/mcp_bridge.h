// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MCP_BRIDGE_H
#define MCP_BRIDGE_H

#include <QByteArray>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QLocalServer;
class QLocalSocket;

// Host interface the bridge queries. Plain std::function fields so tests can
// supply a fake host without linking any widget code.
struct McpEditorHost {
    std::function<int()> tabCount;
    std::function<QString(int)> tabTitle;
    std::function<QString(int)> tabPath;          // "" for non-file tabs
    std::function<bool(int)> tabModified;
    std::function<QString(int)> tabText;
    std::function<int(const QString &)> openFile; // → tab index, -1 on failure
    std::function<QString(int *)> selection;      // text; *out = current tab index
    std::function<QString()> workspaceRoot;       // "" when no folder root is set

    // ── v0.1.118 expansive wave. All optional: an unset field makes the
    //    verb answer with an error (or an empty list where documented),
    //    never crash. mainwindow.cpp builds every lambda. ──
    std::function<int()> currentTabIndex;         // -1 when no tabs
    std::function<QString(int)> tabLanguage;
    std::function<QString(int)> tabEncoding;
    std::function<void(int *, int *)> cursorPosition; // 1-based line/col
    std::function<QStringList()> recentFiles;
    std::function<int(const QString &)> newTab;   // initial text ("" = empty) → index, -1 on failure
    std::function<bool(int, int)> gotoLine;       // (tab index, 1-based line)
    std::function<bool(int, const QString &)> setLanguage; // false = unknown language
    std::function<bool(int, int)> compareTabs;    // opens the Compare dialog (non-modal)
    std::function<QString(const QString &, const QString &, QString *)>
        formatText;                               // (kind, text, errorOut) → "" on failure
    std::function<QString()> notesRoot;           // Noter storage root; "" = none
};

// Editor-side MCP bridge: a dedicated QLocalServer, deliberately separate from
// the single-instance server. Newline-delimited JSON; greeting-before-payload.
class McpBridge : public QObject {
    Q_OBJECT
public:
    // Single-instance server name (SingleInstance::serverName()) + "-mcp".
    static QString defaultServerName();

    // serverName override is for tests; production passes nothing.
    explicit McpBridge(McpEditorHost host, QObject *parent = nullptr,
                       const QString &serverName = QString());

    bool isListening() const;
    QString serverName() const { return m_serverName; }

private slots:
    void onNewConnection();

private:
    void onReadyRead(QLocalSocket *client);
    void handleLine(QLocalSocket *client, const QByteArray &line);
    void sendObject(QLocalSocket *client, const QJsonObject &obj);
    void sendResult(QLocalSocket *client, int id, const QJsonObject &result);
    void sendError(QLocalSocket *client, int id, const QString &message);

    void verbOpenFile(QLocalSocket *client, int id, const QJsonObject &args);
    void verbListOpenTabs(QLocalSocket *client, int id);
    void verbReadTab(QLocalSocket *client, int id, const QJsonObject &args);
    void verbGetSelection(QLocalSocket *client, int id);
    void verbSearchProject(QLocalSocket *client, int id, const QJsonObject &args);
    // v0.1.118 expansive wave — read-only or visibly-reversible only.
    void verbGetStatus(QLocalSocket *client, int id);
    void verbAppInfo(QLocalSocket *client, int id);
    void verbListRecentFiles(QLocalSocket *client, int id);
    void verbFindInTab(QLocalSocket *client, int id, const QJsonObject &args);
    void verbNewTab(QLocalSocket *client, int id, const QJsonObject &args);
    void verbGotoLine(QLocalSocket *client, int id, const QJsonObject &args);
    void verbSetLanguage(QLocalSocket *client, int id, const QJsonObject &args);
    void verbCompareTabs(QLocalSocket *client, int id, const QJsonObject &args);
    void verbFormatText(QLocalSocket *client, int id, const QJsonObject &args);
    void verbListNotes(QLocalSocket *client, int id);
    void verbReadNote(QLocalSocket *client, int id, const QJsonObject &args);

    McpEditorHost m_host;
    QLocalServer *m_server = nullptr;
    QString m_serverName;
    QHash<QLocalSocket *, QByteArray> m_buffers;
};

#endif
