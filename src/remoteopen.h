// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REMOTEOPEN_H
#define REMOTEOPEN_H

#include <QByteArray>
#include <QStringList>
#include <functional>

class QLocalSocket;

// Hard cap on a single forwarded-open payload (file list + line + startupId).
// ~4000 max-length paths fit; anything bigger is hostile or corrupt.
extern const int kRemoteOpenPayloadCap;

using RemoteOpenHandler = std::function<void(const QStringList &paths,
                                             int gotoLine,
                                             const QByteArray &startupId)>;

// Wires readyRead-driven accumulation onto one single-instance pipe client.
// Never blocks; parses when the JSON object is complete; size-capped;
// qWarnings on every drop path; deleteLater()s the client when done.
void attachRemoteOpenClient(QLocalSocket *client, RemoteOpenHandler handler);

#endif
