// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_HTTP_EDITOR_SUPPORT_H
#define NOTEPATRA_HTTP_EDITOR_SUPPORT_H

// Editor-side half of the .http workflow. For a tab whose file ends in
// .http or .rest it shows a clickable "send" marker (a Scintilla triangle)
// on every request line, in margin 4 (unused by the editor otherwise; the
// bookmark, fold and change-history margins are left alone), and adds
// "Send Request" to the editor's context menu. It only emits
// sendRequested(); MainWindow routes that to the REST Client panel.

#include <QObject>
#include <QTimer>

class Editor;
class QMenu;
class QPoint;

class HttpEditorSupport : public QObject {
    Q_OBJECT
public:
    static constexpr int kMargin = 4;
    static constexpr int kMarker = 9;

    // Returns the support object of `ed`, creating it on first use
    // (`*created` tells which). nullptr for a null editor.
    static HttpEditorSupport *ensure(Editor *ed, bool *created = nullptr);
    static bool isHttpPath(const QString &path);

    bool isActive() const { return m_active; }
    Editor *editor() const { return m_ed; }
    // Re-applies the marker colours after a theme change.
    void refreshMarkerColors();
    // Re-parses now instead of after the debounce (tests).
    void rescanNow();

signals:
    void sendRequested(Editor *editor, int line);
    void activeChanged(bool active);

private:
    explicit HttpEditorSupport(Editor *ed);
    void refreshIdentity();
    void onMarginClicked(int margin, int line, Qt::KeyboardModifiers);
    void onContextMenu(QMenu *menu, const QPoint &pos);

    Editor *m_ed = nullptr;
    bool m_active = false;
    QTimer m_rescan;
};

#endif  // NOTEPATRA_HTTP_EDITOR_SUPPORT_H
