// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILE_OPEN_FILTER_H
#define FILE_OPEN_FILTER_H

#include <QObject>
#include <QStringList>
#include <functional>

class QFileOpenEvent;

// Routes macOS QEvent::FileOpen to a handler; queues paths until one is set.
class FileOpenFilter : public QObject {
public:
    using Handler = std::function<void(const QStringList &paths)>;

    explicit FileOpenFilter(QObject *parent = nullptr);

    // Installs the sink and flushes any queued paths into it.
    void setHandler(Handler handler);
    QStringList pendingPaths() const { return m_pending; }

    // Local path carried by the event, or empty for a non-file URL.
    static QString pathFromEvent(const QFileOpenEvent *ev);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Handler m_handler;
    QStringList m_pending;
};

#endif // FILE_OPEN_FILTER_H
