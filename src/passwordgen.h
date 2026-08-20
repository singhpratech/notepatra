// SPDX-License-Identifier: GPL-3.0-or-later
//
// v0.1.128 — Password Generator panel.
//
// Opens as a tab from the Built-in Tools toolbar (next to AI) or
// Tools > Password Generator. All randomness and all character maths
// live in passwordgen_core.{h,cpp}; this file is only widgets.
//
// Nothing generated here is written to disk, sent to an AI backend, or
// restored on the next launch — the tab holds a plain QWidget, so the
// session writer (which serialises editor tabs only) never sees it.

#pragma once

#include "passwordgen_core.h"

#include <QColor>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;

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

    // Whatever is currently shown, or an empty string.
    QString currentText() const;

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

private:
    PasswordGen::Options collectOptions() const;
    void refreshReadout();
    void applyTheme();
    void updateActions();
    void fitReadoutHeight(int lines);
    QWidget *buildCharactersGroup();
    QWidget *buildPassphraseGroup();

    QRadioButton   *m_modeChars = nullptr;
    QRadioButton   *m_modePhrase = nullptr;

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

    QString         m_clipboardOwned;  // what we last put on the clipboard
};
