// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QByteArray>

class QLocalSocket;

namespace SingleInstance {

// One-byte greeting the primary writes on accept, from its event loop —
// receiving it proves the primary is alive and pumping events.
inline constexpr char kAckByte = '\x06';

enum class ForwardResult {
    NoServer,   // could not connect — no reachable primary
    Acked,      // greeting received, full payload handed to the kernel
    NoAck       // no greeting / payload undeliverable — NOTHING was sent
                // that the primary could later act on (safe to fall back)
};

// Per-user local-IPC name (SHA1 of home path, 16 hex chars). Moved verbatim
// from main.cpp's static singleInstanceServerName().
QString serverName();

// Secondary side: connect -> wait for the greeting byte -> send payload ->
// drain the write buffer. The payload is only ever sent to a primary that
// has proven its event loop is alive; a hung primary receives zero bytes.
ForwardResult forwardToPrimary(const QString &name,
                               const QByteArray &payload,
                               int connectMs = 500,
                               int retryConnectMs = 1500,
                               int ackMs = 3000);

// Primary side: write the ACK byte on an accepted client socket.
void ackClient(QLocalSocket *client);

#ifdef Q_OS_WIN
// Creates/opens the Local\<serverName> kernel mutex. Returns the HANDLE as
// void* (nullptr on hard failure). *alreadyExists=true means another process
// holds the singleton (ERROR_ALREADY_EXISTS, or ACCESS_DENIED conservatively).
void *acquireSingletonMutex(const QString &serverName, bool *alreadyExists);
#endif

} // namespace SingleInstance
