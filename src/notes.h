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
#include <QPointer>
#include <QDateTime>
#include <QObject>

class QStackedWidget;
class QSplitter;
class QLabel;
class QToolButton;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QTimer;
class QFrame;
class QVBoxLayout;
class QHBoxLayout;
class QTextEdit;
class QEvent;
class QComboBox;

class NotesStorage;          // src/notes_storage.h
class NotesTodos;            // src/notes_todos.h
class NoterTodosPanel;       // src/notes_panels.h
class NoterPopOut;           // src/notes_popout.h

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

    // Folder paths.
    QString notesRoot() const;
    QString inboxFolder() const;
    QString trashFolder() const;

signals:
    void noteSaved(const QString &absolutePath);
    void noteTitleChanged(const QString &title);

private slots:
    void onAutoSaveTick();
    void onMeetingItemActivated(QListWidgetItem *item);
    void onSearchChanged(const QString &text);
    void onNewMeetingClicked();
    void onTodosToggleClicked();
    void onExtractClicked();
    void onEditorBodyChanged();

private:
    // UI construction
    void buildUi();
    QWidget *buildSidebar();
    QWidget *buildEditorPage();
    QWidget *buildEmptyPage();

    // Sidebar refresh — scans inboxFolder, groups by mtime
    void refreshSidebar();
    void refreshTodosBadge();

    // Show / hide UI states
    void showEmptyPage();
    void showEditorPage();

    // Action handlers
    void renderNoteAtPath(const QString &absolutePath);
    void insertCheckboxAtCursor();
    void toggleCheckboxOnCurrentLine();
    void runMarkdownShortcuts();        // post-textChanged hook
    void toggleSidebar();
    void toggleTodosPane();
    void quickSwitchMeeting();

    // QTextEdit event filter — click ☐/✓ toggles, F4 hotkey,
    // markdown auto-replacement, etc.
    bool eventFilter(QObject *watched, QEvent *event) override;

    // Helpers
    void ensureNotesFolder();
    QString defaultNotesFolder() const;
    QString slugifyTitle(const QString &title) const;

    // ── widgets ────────────────────────────────────────────────────
    QSplitter      *m_splitter      { nullptr };

    // Sidebar
    QWidget        *m_sidebar       { nullptr };
    QLineEdit      *m_search        { nullptr };
    QPushButton    *m_newBtn        { nullptr };
    QListWidget    *m_meetingList   { nullptr };
    QPushButton    *m_todosBtn      { nullptr };
    QLabel         *m_todosBadge    { nullptr };

    // Right side: stack of [empty | editor]
    QStackedWidget *m_rightStack    { nullptr };
    QWidget        *m_emptyPage     { nullptr };
    QWidget        *m_editorPage    { nullptr };
    QTextEdit      *m_editor        { nullptr };
    QLabel         *m_savedHint     { nullptr };  // "saved 2s ago"
    QPushButton    *m_extractBtn    { nullptr };  // floating bottom-right
    QComboBox      *m_modelCombo    { nullptr };  // AI model selector — sits beside Extract
    QWidget        *m_editorFooter  { nullptr };  // holds modelCombo + extractBtn

    // Optional third pane — only constructed when first toggled.
    NoterTodosPanel *m_todosPane    { nullptr };

    // Pop-out — at most one alive.
    NoterPopOut    *m_popOut        { nullptr };

    // ── state ─────────────────────────────────────────────────────
    QString        m_currentPath;
    QString        m_currentTitle;
    bool           m_dirty          { false };
    bool           m_loadingInProgress { false };  // suppress dirty during setHtml
    QDateTime      m_lastSavedAt;

    QPointer<QTimer> m_autosave;

    // ── services (lazily created) ─────────────────────────────────
    NotesStorage  *m_storage        { nullptr };
    NotesTodos    *m_todos          { nullptr };

    // Pull todos from NotesTodos + push to NoterTodosPanel (when visible).
    void refreshTodosPanel();
};
