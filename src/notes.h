// notes.h — Noter: meeting thinkpad embedded in Notepatra.
//
// v0.1.95 redesign — two-pane (sidebar + editor) inspired by Apple Notes /
// Bear / Granola. No slash menu, no insert-button bar, no header button
// row, no edge strip. Plain QTextEdit body with markdown shortcuts; one
// "✨ Extract" button surfaces AI todos+reminders extraction as a
// preview dialog. Reminders panel folded into the optional Todos pane
// (overdue / today / this week / someday / done groups).
//
// On-disk artifact stays HTML in <Documents>/Notepatra/Noter/Inbox/ so
// notes are portable and human-readable. The SQLite todos cache is a
// rebuildable view of those files.
//
// Scope rules (see feedback_notes_tool_scope_lock memory):
//   - Noter tab is the sandbox; nothing here touches global menus,
//     keybindings of other features, the AI dock, or other tab types.
//   - Voice/audio capture is permanently dropped (legal surface — see
//     feedback_no_voice_recording_in_notes).
//   - HTML on disk is the artifact; SQLite is a rebuildable cache.
//   - Reminders attach to todos (the SQLite rows), not to notes.

#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QPointer>
#include <QDateTime>
#include <QObject>
#include <QVector>

class QStackedWidget;
class QSplitter;
class QLabel;
class QToolButton;
class QPushButton;
class QListWidget;       // legacy — kept for now, not used by the new tree sidebar
class QListWidgetItem;   // legacy
class QTreeWidget;
class QTreeWidgetItem;
class QTextBlock;
class QLineEdit;
class QTimer;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QTextEdit;
class QEvent;
class OllamaClient;
class QComboBox;

class NotesStorage;          // src/notes_storage.h
class NotesTodos;            // src/notes_todos.h
class NotesReminderEngine;   // src/notes_reminder.h
class NoterTodosPanel;       // src/notes_panels.h
class NoterPopOut;           // src/notes_popout.h
struct TodoRow;

class NotesPanel : public QWidget {
    Q_OBJECT
public:
    explicit NotesPanel(QWidget *parent = nullptr);
    ~NotesPanel() override;

    // Open a brand-new meeting note. Creates the file under
    // <notesRoot>/Inbox/ with a slugged filename based on today's
    // date + the title placeholder, then loads it.
    void newMeetingNote();

    // Load and display a note from disk. Path may be relative to the
    // notes root or absolute. Persists the previous note first.
    void openNoteFile(const QString &absolutePath);

    // Persist current edits to disk. Atomic write via .tmp + rename.
    // Called by the autosave timer (every 5s while dirty), and on
    // tab/window close.
    void saveCurrentNote();

    // Pop out the active note into a borderless floating window
    // (Ctrl+Alt+P). Power-user shortcut only — no UI button.
    void popOutActive();

    // Run the AI extraction over the note body + show the preview
    // dialog. Wired to the ✨ Extract button AND Ctrl+Alt+E.
    void endMeetingSweep();

    // Show the Extract review dialog for an AI response + apply the result.
    // MUST be invoked deferred (singleShot) from the finished handler, never
    // synchronously inside the signal — see the crash note in notes.cpp.
    // Public so the widget test can drive it without a live network client.
    void showExtractResult(const QString &response, const QString &model);

    // Folder paths.
    QString notesRoot() const;
    QString inboxFolder() const;
    QString trashFolder() const;

    // The standalone Todos checklist file (Inbox/quick-todos.html) and the
    // action that opens it in the editor AS AN EDITABLE CHECKLIST (☐/✓
    // lines) rather than navigating to a todo's source meeting. User-asked
    // 2026-05-24: "todo should open and edit on its own like a meeting, it
    // should not go to the meeting — checklist todo list."
    QString todosChecklistPath() const;
    void openTodosChecklist();

    // v0.1.98 — set / change / clear a reminder bound to a note file
    // (right-click a Noter → Set reminder). Public so the widget test can
    // drive the modal under a watchdog.
    void promptReminderForNote(const QString &notePath, const QString &title);

    // v0.1.98 — editor toolbar / right-click → "Insert header". Drops a bold
    // heading at the given level (1=H1 biggest … 3=H3 smallest) and ALWAYS
    // starts the section with a "☐ " checkbox bullet below it (user: every
    // section has a checkable/cancellable bullet). Empty title → prompt for it.
    // Public so the widget test can assert the contract.
    void insertSubheader(const QString &title, int level = 2);

signals:
    void noteSaved(const QString &absolutePath);
    void noteTitleChanged(const QString &title);

private slots:
    void onAutoSaveTick();
    void onSidebarItemActivated(QTreeWidgetItem *item, int column);
    void onSearchChanged(const QString &text);
    void onNewMeetingClicked();
    void onExtractClicked();
    void onEditorBodyChanged();

private:
    // UI construction
    void buildUi();
    QWidget *buildSidebar();
    QWidget *buildEditorPage();
    QWidget *buildEmptyPage();

    // Sidebar refresh — v0.1.97 tree shape. Rebuilds the QTreeWidget
    // from disk + the todos cache. Preserves the user's expand/collapse
    // state by keying off item-text.
    void refreshSidebar();
    void populateMeetingsRoot(QTreeWidgetItem *root, const QString &filter);
    void populateTodosRoot(QTreeWidgetItem *root, const QString &filter);
    void populateTrashRoot(QTreeWidgetItem *root, const QString &filter);
    // v0.1.98 — central "Reminders" root: every scheduled reminder (note-level
    // + per-action), grouped Overdue / Today / This week / Later. Each leaf
    // opens its source note; pencil changes the time; ✕ deletes it.
    void populateRemindersRoot(QTreeWidgetItem *root, const QString &filter);

    // Shared date+time picker (calendar popup + quick-pick chips). Returns the
    // chosen time, or invalid if cancelled. *cleared is set true if the user
    // hit "Clear reminder" (only offered when allowClear is true).
    QDateTime pickReminderDateTime(const QString &title, const QDateTime &initial,
                                   bool allowClear, bool *cleared);
    // Change (or clear) the time of an existing reminder row by id.
    void changeReminderTime(const QString &id);
    // Expand + scroll to the Reminders root so a just-set reminder is visible.
    void expandRemindersRoot();

    // v0.1.98 — (re)fetch the AI model list for the footer dropdown from the
    // CURRENTLY-configured backend. Called at build AND on every showEvent so
    // switching the backend in the AI panel is reflected in the Noter dropdown.
    void refreshNoterModels();

    // Show / hide UI states
    void showEmptyPage();
    void showEditorPage();

    // Serialize the editor's ☐/✓ checklist lines back to b-act divs,
    // re-attaching each line's id / owner / due (current value pulled from
    // the todos DB) so the structured todo + reminder data survives a
    // checklist edit. Called by saveCurrentNote when m_currentIsChecklist.
    void saveTodosChecklist();

    // Action handlers
    void renderNoteAtPath(const QString &absolutePath);
    void insertCheckboxAtCursor();
    void toggleCheckboxOnCurrentLine();
    // v0.1.98 — strike through + mute a checklist line when it's checked
    // (done), restore it when reopened. Styles the text after the marker.
    void applyChecklistDoneStyle(const QTextBlock &block, bool done);
    // Re-derive ✓ strike-through across the whole document on load (the
    // marker survives save/reload but the rich format is sanitized out).
    void restyleChecklistLines();
    void runMarkdownShortcuts();        // post-textChanged hook
    void toggleSidebar();
    void quickSwitchMeeting();

    // v0.1.97 — cross-platform desktop notification. Routes through
    // QSystemTrayIcon::showMessage which Qt translates to:
    //   - Linux: libnotify / D-Bus org.freedesktop.Notifications
    //   - macOS: NSUserNotificationCenter (Qt5) or UNUserNotificationCenter
    //   - Windows: Windows Action Center Toast
    // One code path, three platforms. Returns false if the platform
    // doesn't expose a tray notification daemon (rare — every modern
    // desktop has one).
    bool fireDesktopNotification(const QString &title, const QString &body);

    // v0.1.97 — in-window reminder banner. enqueueReminder pushes a
    // fired reminder onto the queue and shows the banner (flashing) if
    // it's idle. Dismiss / Snooze advance to the next queued reminder.
    void enqueueReminder(const TodoRow &r);
    void showNextReminder();
    void hideReminderBanner();
    QVector<TodoRow> m_reminderQueue;

    // QTextEdit event filter — click ☐/✓ toggles, F4 hotkey,
    // markdown auto-replacement, etc.
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Auto-tidy the sidebar each time Noter becomes visible: the Trash
    // archive is force-collapsed so it never greets the user as a wall of
    // trashed rows (user-requested 2026-05-24). Meetings + Todos keep
    // their in-session expand state.
    void showEvent(QShowEvent *event) override;

    // Helpers
    void ensureNotesFolder();
    QString defaultNotesFolder() const;
    QString slugifyTitle(const QString &title) const;

    // ── widgets ────────────────────────────────────────────────────
    QSplitter      *m_splitter      { nullptr };

    // Sidebar — v0.1.97 tree shape: 3 expandable roots (Meetings,
    // Todos, Trash). Click a leaf to open. Right-click any node for
    // its context menu. Drops the prior bottom All Todos button.
    QWidget        *m_sidebar       { nullptr };
    QLineEdit      *m_search        { nullptr };
    QPushButton    *m_newBtn        { nullptr };
    QTreeWidget    *m_sidebarTree   { nullptr };

    // Right side: stack of [empty | editor]
    QStackedWidget *m_rightStack    { nullptr };
    QWidget        *m_emptyPage     { nullptr };
    QWidget        *m_editorPage    { nullptr };
    QTextEdit      *m_editor        { nullptr };
    QLabel         *m_savedHint     { nullptr };  // "saved 2s ago"
    QPushButton    *m_extractBtn    { nullptr };  // floating bottom-right
    QComboBox      *m_modelCombo    { nullptr };  // AI model selector — sits beside Extract
    // v0.1.98 — long-lived model-list client, reused on every refresh so the
    // dropdown re-fetches from the CURRENT backend (it used to list once at
    // startup and go stale when the backend changed in the AI panel).
    OllamaClient   *m_modelListClient { nullptr };
    QWidget        *m_editorFooter  { nullptr };  // holds modelCombo + extractBtn

    // v0.1.97 — reminder banner. Appears at the top of the editor page
    // when a reminder fires. Flashes via the timer until the user clicks
    // Dismiss (marks reminder_status='dismissed' in SQLite) or Snooze
    // (pushes reminder_at by 10 min). Multiple concurrent reminders
    // queue — dismiss reveals the next.
    QWidget        *m_reminderBanner       { nullptr };
    QLabel         *m_reminderLabel        { nullptr };
    QPushButton    *m_reminderDismissBtn   { nullptr };
    QPushButton    *m_reminderSnoozeBtn    { nullptr };
    QPushButton    *m_reminderOpenSrcBtn   { nullptr };
    QPointer<QTimer> m_reminderFlashTimer;
    bool             m_reminderFlashOn     { false };

    // Optional third pane — only constructed when first toggled.
    NoterTodosPanel *m_todosPane    { nullptr };

    // Pop-out — at most one alive.
    NoterPopOut    *m_popOut        { nullptr };

    // ── state ─────────────────────────────────────────────────────
    QString        m_currentPath;
    QString        m_currentTitle;
    bool           m_dirty          { false };
    // v0.1.98 — when the open document is the Todos checklist we edit it as
    // plain ☐/✓ lines (which round-trip through QTextEdit cleanly, unlike
    // the b-act data-* divs). m_checklistBlocks holds the blocks parsed on
    // open, in order, so save can re-attach each line's stable id / due /
    // owner (which the structured todo + reminder system keys off) instead
    // of regenerating them every edit.
    bool           m_currentIsChecklist { false };
    struct ChecklistBlock { QString id; QString owner; QString dueIso; QString status; QString text; };
    QVector<ChecklistBlock> m_checklistBlocks;
    // v0.1.96+ — which date-bucket sections in the meeting sidebar are
    // currently collapsed. Click a header to toggle; refreshSidebar()
    // hides items whose section is in this set.
    QSet<QString>  m_collapsedSections;
    bool           m_loadingInProgress { false };  // suppress dirty during setHtml
    bool           m_loadingTree       { false };  // suppress itemChanged during refreshSidebar
    QDateTime      m_lastSavedAt;

    QPointer<QTimer> m_autosave;

    // ── services (lazily created) ─────────────────────────────────
    NotesStorage         *m_storage   { nullptr };
    NotesTodos           *m_todos     { nullptr };
    // v0.1.97 — reminder engine. Pre-fix the class existed but was
    // never instantiated; reminders set via right-click → Set Reminder
    // went into SQLite but the 60s poll loop never ran, so desktop
    // notifications never fired on any platform.
    NotesReminderEngine  *m_reminders { nullptr };

    // v0.1.97 — refreshTodosPanel removed; todos render inline in the
    // sidebar tree via populateTodosRoot() instead of a separate pane.
};
