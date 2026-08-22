// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.129 — Password Generator panel.
//
// Opens as a tab from the Built-in Tools toolbar (next to AI) or
// Tools > Password Generator. All randomness and all character maths
// live in passwordgen_core.{h,cpp}; this file is only widgets.
//
// A left rail picks between three pages: Password, Passphrase and
// SSH key. The tab title never carries a generated value — it is
// visible over MCP and in the window title.
//
// Nothing generated here is written to disk, sent to an AI backend, or
// restored on the next launch — the tab holds a plain QWidget, so the
// session writer (which serialises editor tabs only) never sees it.

#pragma once

#include "passwordgen_core.h"

#include <QColor>
#include <QFutureWatcher>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QKeyEvent;
class QSlider;
class QSpinBox;
class QStackedWidget;

// One generated SSH key, copied out of the Rust result before that result
// is freed. Crosses a thread boundary, so it holds only value types.
struct SshKeyOut {
    bool    ok = false;
    QString priv;
    QString pub;
    QString fp;
    QString err;
};

// Five-segment strength meter. Painted rather than styled so it reads
// the same on Light, Dark and Monokai without a stylesheet per theme.
class StrengthBar : public QWidget {
    Q_OBJECT
public:
    explicit StrengthBar(QWidget *parent = nullptr);
    void setScore(int score);   // 0..4, -1 for "nothing yet"
    void setEmptyColour(const QColor &c);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int    m_score = -1;
    QColor m_empty;
};

class PasswordGenPanel : public QWidget {
    Q_OBJECT
public:
    explicit PasswordGenPanel(QWidget *parent = nullptr);

    // Whatever is currently shown, or an empty string. On the SSH page
    // this is the PUBLIC key line — never the private key.
    QString currentText() const;

    // Which rail page is showing. Ordered as the rail stacks them.
    enum Page { PagePassword = 0, PagePassphrase = 1, PageSsh = 2 };
    void selectPage(int page);
    int currentPage() const;

    // Writes the current private key to `path` and the public line to
    // `path`.pub. Refuses if either exists. Exposed so a test can drive
    // the write without opening a file dialog.
    bool writeSshKeyTo(const QString &path);

signals:
    // Insert at the caret of the active editor tab.
    void insertRequested(const QString &text);
    // Open the current batch as a new untitled editor tab.
    void newTabRequested(const QString &text);
    // Transient message for the main-window status bar.
    void statusMessage(const QString &text);

public slots:
    void regenerate();
    // Panels self-theme: the app-wide stylesheet only covers QMainWindow,
    // menus, the toolbar, tabs and scrollbars, so without this the panel
    // renders light-on-dark.
    void onThemeChanged();

private slots:
    void copyToClipboard();
    void clearClipboardIfUnchanged();
    void syncEnabledState();
    void generateSshKey();
    void onSshKeyReady();
    void syncSshEnabledState();

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    PasswordGen::Options collectOptions() const;
    void refreshReadout();
    void applyTheme();
    void updateActions();
    void fitReadoutHeight(int lines);
    QWidget *buildCharactersGroup();
    QWidget *buildPassphraseGroup();
    QWidget *buildRail();
    QWidget *buildSshPage();
    void applyRailState();
    void clearSshResult();
    void saveSshPrivateKey();
    void setSshStatus(const QString &text);
    int  sshAlg() const;
    int  sshBits() const;
    QString sshDefaultFileName() const;

    QPlainTextEdit *m_out = nullptr;
    StrengthBar    *m_bar = nullptr;
    QLabel         *m_entropy = nullptr;
    QLabel         *m_status = nullptr;

    QWidget        *m_charsGroup = nullptr;
    QWidget        *m_phraseGroup = nullptr;

    QSlider        *m_length = nullptr;
    QSpinBox       *m_lengthBox = nullptr;
    QSpinBox       *m_count = nullptr;
    QCheckBox      *m_lower = nullptr;
    QCheckBox      *m_upper = nullptr;
    QCheckBox      *m_digits = nullptr;
    QCheckBox      *m_symbols = nullptr;
    QLineEdit      *m_extra = nullptr;
    QCheckBox      *m_noLookalikes = nullptr;
    QCheckBox      *m_requireEach = nullptr;

    QSpinBox       *m_words = nullptr;
    QComboBox      *m_separator = nullptr;
    QCheckBox      *m_capitalise = nullptr;
    QCheckBox      *m_appendDigits = nullptr;

    QPushButton    *m_copyBtn = nullptr;
    QPushButton    *m_insertBtn = nullptr;
    QPushButton    *m_newTabBtn = nullptr;

    // ── Rail + stack ───────────────────────────────────────────────
    QWidget           *m_rail = nullptr;
    QList<QPushButton *> m_railItems;
    QStackedWidget    *m_stack = nullptr;
    QWidget           *m_valueArea = nullptr;   // readout + meter, hidden on the SSH page
    QWidget           *m_actionRow = nullptr;   // Generate/Insert/New tab, likewise
    QSpinBox          *m_count2 = nullptr;      // the Passphrase page copy of "How many"

    // ── SSH key page ───────────────────────────────────────────────
    QComboBox      *m_sshType = nullptr;
    QLineEdit      *m_sshComment = nullptr;
    QLineEdit      *m_sshPass = nullptr;
    QLineEdit      *m_sshPass2 = nullptr;
    QCheckBox      *m_sshShowPass = nullptr;
    QPushButton    *m_sshGenBtn = nullptr;
    QWidget        *m_sshResults = nullptr;
    QPlainTextEdit *m_sshPublic = nullptr;
    QLabel         *m_sshFingerprint = nullptr;
    QPushButton    *m_sshCopyPubBtn = nullptr;
    QPushButton    *m_sshInsertBtn = nullptr;
    QPushButton    *m_sshNewTabBtn = nullptr;
    QPushButton    *m_sshSaveBtn = nullptr;
    QCheckBox      *m_sshShowPrivate = nullptr;
    QWidget        *m_sshPrivateBox = nullptr;
    QPlainTextEdit *m_sshPrivate = nullptr;
    QPushButton    *m_sshCopyPrivBtn = nullptr;
    QLabel         *m_sshStatus = nullptr;
    QLabel         *m_sshSecurity = nullptr;

    QString         m_sshPrivatePem;   // wiped best effort before reassignment
    QString         m_sshPublicLine;
    QString         m_sshFpText;
    QString         m_sshStatusText;   // survives the mismatch message
    bool            m_sshBusy = false;
    QFutureWatcher<SshKeyOut> *m_sshWatcher = nullptr;

    QString         m_clipboardOwned;  // what we last put on the clipboard
    QString         m_privateShown;    // the PEM, so the watcher never arms on a public key
};
