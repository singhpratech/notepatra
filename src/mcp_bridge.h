// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MCP_BRIDGE_H
#define MCP_BRIDGE_H

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QEvent;
class QFrame;
class QLocalServer;
class QLocalSocket;
class QTimer;
class QWidget;

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

    // ── v0.1.118 WRITE tier. Every one of these mutates a buffer, so the
    //    bridge runs them ONLY after a human clicks Approve on the in-window
    //    card (no backdoor, no auto-approve, ever). approvalParent supplies
    //    the widget that hosts the card; unset = every write verb refused. ──
    std::function<QWidget *()> approvalParent;
    std::function<bool(int)> hasSelection;
    // (tab, 1-based line, 1-based col, text); line -1 = insert at cursor.
    std::function<bool(int, int, int, const QString &)> insertText;
    std::function<bool(int, const QString &)> replaceSelection;
    // Literal (non-regex) find/replace; → occurrence count, -1 on failure.
    std::function<int(int, const QString &, const QString &, bool)> applyEdit;
    std::function<bool(int)> saveTab;             // path-ful tabs only

    // ── v0.1.119 depth wave. Same rule as above: every field is optional;
    //    an unset field makes the verb answer with a clear "not supported by
    //    host" error, never crash. mainwindow.cpp builds each lambda over the
    //    REAL app code path (git_tools, DbConnections classifier + runQuery,
    //    NotesStorage/NotesTodos, DiagramView). The bridge stays widget-free
    //    and never links those heavy TUs — only the pure Npd::parse. ──

    // READ tier — answered immediately, no card.
    // Raw (unbucketed) Noter reminders: [{note_file, note_title, due_iso}].
    // The bridge computes the Overdue/Today/This week/Later bucket itself so
    // the wire contract is stable regardless of the UI's grouping code.
    std::function<QJsonArray()> reminders;
    // ONE fixed read-only git subcommand (chosen by the bridge, NEVER the
    // client): sub ∈ {status,diff,log,show,branch}. Returns the git_tools
    // payload text; *err set (non-empty) on failure. Reuses GitTools — no
    // new QProcess path, no way to reach a write subcommand.
    std::function<QString(const QString &sub, const QJsonObject &args,
                          QString *err)>
        runGit;
    // If tab i is an .npd diagram tab: sets *outSource to its source and
    // returns true; false otherwise (non-diagram tab / out of range).
    std::function<bool(int, QString *)> diagramSource;
    // SELECT-only query through the Data-Analyst engine. The caller has
    // ALREADY been classified read-only by the bridge; the host re-asserts
    // it (runQuery allowMutation=false) as defense in depth. Returns
    // {columns:[...], rows:[[...]], truncated:bool, engine:"..."}; *err on
    // failure. csvPath "" ⇒ in-memory SQLite; else the CSV is the source.
    std::function<QJsonObject(const QString &sql, const QString &csvPath,
                              QString *err)>
        runSql;

    // ACT tier — visible, non-destructive, NO approval card.
    // Open a Noter note (already confirmed inside the Noter root by the
    // bridge) in the Noter tab UI. Returns the note's display title; *err
    // on failure.
    std::function<QString(const QString &absFile, QString *err)> openNote;

    // WRITE tier — human-approval-gated, same enqueueApproval flow.
    // Create a Noter note the way Noter does (HTML shell, Inbox folder,
    // unique <stamp>-noter-NN.html name). Returns the new absolute path;
    // *err on failure.
    std::function<QString(const QString &title, const QString &body,
                          QString *err)>
        createNote;
    // Append a paragraph of text to an existing note's body (path already
    // Noter-root-confined by the bridge). false + *err on failure.
    std::function<bool(const QString &absFile, const QString &text,
                       QString *err)>
        appendNote;
    // Bind a reminder to a note exactly like the UI (same SQLite storage,
    // fires a desktop notification on its poll). false + *err on failure.
    std::function<bool(const QString &absFile, const QDateTime &due,
                       QString *err)>
        setReminder;
    // Render an open .npd tab to path in format ("png"|"pdf") via the real
    // DiagramView export path. false + *err on failure.
    std::function<bool(int tabIndex, const QString &path,
                       const QString &format, QString *err)>
        exportDiagram;

    // ── Phase 0A. Optional like everything above: unset ⇒ error/empty,
    //    never crash. ──
    // Canonicalize an agent language token ("python"→"Python"); "" = unknown.
    std::function<QString(const QString &)> resolveLanguage;
    // Canonical Language-menu tokens (SSOT: allKnownLanguageTokens()).
    std::function<QStringList()> knownLanguages;

    // ── Phase 1: diagram control + Noter panel. Optional like everything
    //    above: unset ⇒ clear error, never crash. ──
    // ACT: create a Diagram tab (optionally pre-filled), focus it; → index, -1 on failure.
    std::function<int(const QString &source, const QString &title)> createDiagram;
    // WRITE: replace the .npd source of the DiagramEditor at tab i; false = not a diagram.
    std::function<bool(int, const QString &)> setDiagramSource;
    // ACT: open/focus the Noter panel tab (same path as the "+ Noter" UI).
    std::function<bool()> openNoter;

    // ── Phase 2: Data-analyst + Charts. Optional: unset ⇒ clear error. ──
    // READ: sanitized saved connections [{name,driver,database,read_only}] — never credentials.
    std::function<QJsonArray()> listConnections;
    // READ: SELECT-only query on a SAVED connection via classifySql + runQuery(allowMutation=false).
    std::function<QJsonObject(const QString &name, const QString &sql,
                              int maxRows, QString *err)> runNamedQuery;
    // READ: user tables over a saved connection; *err on open failure.
    std::function<QJsonArray(const QString &name, QString *err)> listTables;
    // Pure static classification (DbConnections::classifySql, restrictFilesystem=true); false ⇒ *reason.
    std::function<bool(const QString &sql, QString *reason)> classifySqlReadOnly;
    // ACT: reveal the AI dock in Data Analyst mode.
    std::function<bool()> openDataAnalyst;
    // ACT: inline chart card in the Data transcript → {chart_id,rendered}; *err on failure/Lite.
    std::function<QJsonObject(const QJsonObject &spec, const QString &title,
                              QString *err)> renderChart;
    // WRITE: off-screen render + write chart bytes to path; false + *err on failure/Lite.
    std::function<bool(const QJsonObject &spec, const QString &path,
                       const QString &format, int scale, QString *err)> exportChart;
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
    ~McpBridge() override;

    bool isListening() const;
    QString serverName() const { return m_serverName; }

    // Test hook: shrink the human-approval timeout (production default 120 s).
    void setApprovalTimeoutMs(int ms);

protected:
    // Repositions the approval card when its host widget resizes.
    bool eventFilter(QObject *watched, QEvent *event) override;

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
    // v0.1.118 WRITE tier — every verb below is human-approval-gated.
    void verbInsertText(QLocalSocket *client, int id, const QJsonObject &args);
    void verbReplaceSelection(QLocalSocket *client, int id,
                              const QJsonObject &args);
    void verbApplyEdit(QLocalSocket *client, int id, const QJsonObject &args);
    void verbSaveTab(QLocalSocket *client, int id, const QJsonObject &args);
    // ── v0.1.119 depth wave ──
    // READ tier.
    void verbListReminders(QLocalSocket *client, int id);
    // sub is the bridge-fixed git subcommand token; the client never picks it.
    void verbGit(QLocalSocket *client, int id, const QJsonObject &args,
                 const QString &sub);
    void verbValidateNpd(QLocalSocket *client, int id, const QJsonObject &args);
    void verbRunSql(QLocalSocket *client, int id, const QJsonObject &args);
    // ── Phase 0A read tier ──
    void verbListLanguages(QLocalSocket *client, int id);
    void verbGetCapabilities(QLocalSocket *client, int id);
    // ACT tier.
    void verbOpenNote(QLocalSocket *client, int id, const QJsonObject &args);
    // WRITE tier — human-approval-gated.
    void verbCreateNote(QLocalSocket *client, int id, const QJsonObject &args);
    void verbAppendNote(QLocalSocket *client, int id, const QJsonObject &args);
    void verbSetReminder(QLocalSocket *client, int id, const QJsonObject &args);
    void verbExportDiagram(QLocalSocket *client, int id,
                           const QJsonObject &args);
    // ── Phase 1 ──
    void verbCreateDiagram(QLocalSocket *client, int id, const QJsonObject &args);   // ACT
    void verbGetDiagramSource(QLocalSocket *client, int id, const QJsonObject &args); // READ
    void verbSetDiagramSource(QLocalSocket *client, int id, const QJsonObject &args); // WRITE (card)
    void verbOpenNoter(QLocalSocket *client, int id);                                 // ACT
    // ── Phase 2: Data-analyst + Charts ──
    void verbListConnections(QLocalSocket *client, int id);                            // READ
    void verbRunQuery(QLocalSocket *client, int id, const QJsonObject &args);          // READ
    void verbListTables(QLocalSocket *client, int id, const QJsonObject &args);        // READ
    void verbOpenDataAnalyst(QLocalSocket *client, int id);                            // ACT
    void verbRenderChart(QLocalSocket *client, int id, const QJsonObject &args);       // ACT
    void verbExportQueryResults(QLocalSocket *client, int id, const QJsonObject &args); // WRITE (card)
    void verbExportChart(QLocalSocket *client, int id, const QJsonObject &args);       // WRITE (card)

    // One held write request: the response is sent only after the human
    // decides (or the timeout / a disconnect decides for them).
    struct PendingWrite {
        QPointer<QLocalSocket> client;
        int id = -1;
        QString description;
        QString preview;
        // Runs the mutation on Approve; a non-empty *err means failure.
        std::function<QJsonObject(QString *)> execute;
    };

    int resolveWriteTab(const QJsonObject &args, QString *err) const;
    QString tabLabel(int idx) const;
    // Resolve args["file"] to a canonical .html path that PROVABLY sits
    // inside the Noter root (../ escapes and out-of-root absolutes are
    // rejected). Returns "" + *err on any failure. Shared by read_note and
    // the v0.1.119 note verbs so the containment law lives in one place.
    QString resolveNotePath(const QJsonObject &args, QString *err) const;
    void enqueueApproval(QLocalSocket *client, int id,
                         const QString &description, const QString &preview,
                         std::function<QJsonObject(QString *)> execute);
    void showNextApproval();
    void showApprovalCard(QWidget *parent, const PendingWrite &pw);
    void resolveActiveApproval(bool approved, const QString &denyMessage);
    void dismissActiveCard();
    void dropClientApprovals(QLocalSocket *client);
    void positionCard();

    McpEditorHost m_host;
    QLocalServer *m_server = nullptr;
    QString m_serverName;
    QHash<QLocalSocket *, QByteArray> m_buffers;

    // FIFO approval queue; the front entry is the one on screen when
    // m_approvalActive is true.
    QList<PendingWrite> m_approvalQueue;
    bool m_approvalActive = false;
    QPointer<QFrame> m_card;
    QPointer<QWidget> m_cardParent;
    QTimer *m_approvalTimer = nullptr;
    int m_approvalTimeoutMs = 120000; // 120 s auto-deny
};

#endif
