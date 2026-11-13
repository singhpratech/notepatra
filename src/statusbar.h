// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef STATUSBAR_H
#define STATUSBAR_H

#include <QWidget>
#include <QLabel>

class NppStatusBar : public QWidget {
    Q_OBJECT
public:
    explicit NppStatusBar(QWidget *parent = nullptr);

    void updatePosition(int line, int col, int pos);
    void updateSelection(int chars, int lines);
    void updateLines(int count);
    void updateLength(int length);
    void updateWords(int count);
    void updateLanguage(const QString &lang);
    void updateEncoding(const QString &enc);
    void updateEol(const QString &eol);
    void updateInsertMode(bool overwrite);
    // v0.1.92 — per-tab line-level change history counter shown after the
    // cursor position. modified = lines edited but not saved yet; saved =
    // lines that have been edited at some point during the session and
    // already persisted to disk.
    void updateChangeHistory(int modified, int saved);
    // A tool tab (Welcome, REST Client, Noter, ...) is current: the editor
    // fields and the click-to-change menus are hidden and the language slot
    // shows the tab's name. Hidden slots keep their space, so switching
    // tabs does not shift the bar. showEditor() brings the fields back.
    void showNonEditor(const QString &tabName);
    void showEditor();
    bool showsEditor() const { return m_editorMode; }
    void applyColors(const QString &bg, const QString &fg, const QString &sep);

signals:
    // Fired when the user clicks one of the status-bar indicators —
    // MainWindow pops the appropriate menu at the click location so
    // users can change language / encoding / EOL in one click, the
    // same way VS Code / Sublime / modern editors let them.
    void languageClicked(const QPoint &globalPos);
    void encodingClicked(const QPoint &globalPos);
    void eolClicked(const QPoint &globalPos);

protected:
    void mousePressEvent(QMouseEvent *event) override;

private:
    QLabel *m_lang, *m_size, *m_pos, *m_changes, *m_eol, *m_enc, *m_ins;
    QList<QWidget *> m_editorOnly;  // shown only for an editor tab (not m_changes)
    bool m_editorMode = true;
};

#endif
