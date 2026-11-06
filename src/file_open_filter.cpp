// SPDX-License-Identifier: GPL-3.0-or-later

#include "file_open_filter.h"

#include <QEvent>
#include <QFileOpenEvent>
#include <QUrl>

FileOpenFilter::FileOpenFilter(QObject *parent) : QObject(parent) {}

QString FileOpenFilter::pathFromEvent(const QFileOpenEvent *ev) {
    if (!ev) return QString();
    const QUrl url = ev->url();
    if (url.isValid() && !url.isEmpty() && !url.isLocalFile()) return QString();
    if (url.isLocalFile()) return url.toLocalFile();
    return ev->file();
}

void FileOpenFilter::setHandler(Handler handler) {
    m_handler = std::move(handler);
    if (!m_handler || m_pending.isEmpty()) return;
    const QStringList queued = m_pending;
    m_pending.clear();
    m_handler(queued);
}

bool FileOpenFilter::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() != QEvent::FileOpen) return QObject::eventFilter(watched, event);
    const QString path = pathFromEvent(static_cast<QFileOpenEvent *>(event));
    if (path.isEmpty()) return false;
    if (m_handler) m_handler({path});
    else m_pending.append(path);
    return true;
}
