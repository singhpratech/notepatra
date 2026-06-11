// SPDX-License-Identifier: GPL-3.0-or-later
#include "remoteopen.h"

#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>
#include <memory>

const int kRemoteOpenPayloadCap = 1 << 20;  // 1 MiB

void attachRemoteOpenClient(QLocalSocket *client, RemoteOpenHandler handler) {
    auto buf  = std::make_shared<QByteArray>();
    auto done = std::make_shared<bool>(false);

    // Compact JSON only parses once the full object has arrived, so
    // parse-success doubles as the end-of-payload marker (no length prefix —
    // protocol unchanged, ACK is a separate fix).
    auto tryParse = [client, buf, done, handler]() {
        if (*done) return;
        const QJsonDocument doc = QJsonDocument::fromJson(*buf);
        if (!doc.isObject()) return;  // incomplete — wait for more bytes
        *done = true;
        const QJsonObject o = doc.object();
        QStringList paths;
        for (const QJsonValue &v : o.value("files").toArray())
            paths.append(v.toString());
        handler(paths, o.value("gotoLine").toInt(-1),
                o.value("startupId").toString().toUtf8());
        client->disconnectFromServer();
    };
    auto finalize = [client, buf, done]() {
        if (!*done && !buf->isEmpty())
            qWarning("Notepatra: dropping unparseable remote-open payload (%d bytes)",
                     int(buf->size()));
        *done = true;  // suppress duplicate warning if finalize runs twice
        client->deleteLater();
    };

    QObject::connect(client, &QLocalSocket::readyRead, client,
                     [client, buf, done, tryParse]() {
        buf->append(client->readAll());
        if (!*done && buf->size() > kRemoteOpenPayloadCap) {
            qWarning("Notepatra: remote-open payload exceeded %d bytes — dropping client",
                     kRemoteOpenPayloadCap);
            *done = true;
            // Queued: abort() inside the socket's own readyRead emission
            // tears down its engine mid-signal (observed segfault offscreen).
            QMetaObject::invokeMethod(client, [client]() { client->abort(); },
                                      Qt::QueuedConnection);
            return;
        }
        tryParse();
    });
    QObject::connect(client, &QLocalSocket::disconnected, client,
                     [client, buf, tryParse, finalize]() {
        if (client->isOpen())
            buf->append(client->readAll());  // bytes can ride in with the close
        tryParse();
        finalize();
    });

    // Bytes (or even the close) may have raced ahead of the wire-up — this is
    // exactly the startup-drain case where the sender wrote and exited before
    // the slot existed.
    if (client->bytesAvailable() > 0) buf->append(client->readAll());
    tryParse();
    if (client->state() != QLocalSocket::ConnectedState) finalize();
}
