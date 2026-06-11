// SPDX-License-Identifier: GPL-3.0-or-later
#include "singleinstance.h"
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif

QString SingleInstance::serverName() {
    // Per-user server name: same user's second launch finds the first; two
    // different users on one machine get distinct servers.
    const QByteArray salt = QDir::homePath().toUtf8();
    const QByteArray h = QCryptographicHash::hash(salt, QCryptographicHash::Sha1).toHex();
    return QStringLiteral("notepatra-") + QString::fromLatin1(h.left(16));
}

SingleInstance::ForwardResult SingleInstance::forwardToPrimary(
        const QString &name, const QByteArray &payload,
        int connectMs, int retryConnectMs, int ackMs) {
    QLocalSocket sock;
    QElapsedTimer budget;
    budget.start();
    sock.connectToServer(name);
    if (!sock.waitForConnected(connectMs)) {
        sock.abort();
        // Retry only after a FAST first miss (Defender cold-start case). A slow
        // miss means Windows burned ~5 s in WaitNamedPipe on a busy pipe.
        if (retryConnectMs > 0 && budget.elapsed() < 2000) {
            sock.connectToServer(name);
            sock.waitForConnected(retryConnectMs);
        }
    }
    if (sock.state() != QLocalSocket::ConnectedState)
        return ForwardResult::NoServer;

    // Wait for the primary's greeting BEFORE sending anything. The greeting
    // is written from the primary's event loop on accept, so it proves the
    // loop is pumping. The old order (send, then wait) left the payload
    // deliverable in the kernel pipe after a NoAck abort — a slow-but-alive
    // primary drained it later, AFTER the standalone fallback had already
    // opened the same files: every open happened twice.
    QDeadlineTimer greetDeadline(ackMs);
    while (sock.bytesAvailable() < 1 && !greetDeadline.hasExpired()) {
        if (!sock.waitForReadyRead(int(greetDeadline.remainingTime())))
            break;   // timeout, peer close, or pipe break — all = no greeting
    }
    char greet = 0;
    if (sock.bytesAvailable() < 1 ||
        (sock.read(&greet, 1) != 1 || greet != kAckByte)) {
        sock.abort();   // nothing was sent — the pipe carries no payload
        return ForwardResult::NoAck;
    }

    sock.write(payload);
    sock.flush();
    // Drain the local write buffer completely: returning with bytes still
    // queued destroys the socket and truncates the forward (Windows cancels
    // the pending overlapped write on close).
    QDeadlineTimer writeDeadline(ackMs);
    while (sock.bytesToWrite() > 0 && !writeDeadline.hasExpired()) {
        if (!sock.waitForBytesWritten(int(writeDeadline.remainingTime())))
            break;
    }
    if (sock.bytesToWrite() > 0) {
        // Greeted but stopped reading mid-payload. Abort: a truncated JSON
        // payload can never parse, so the primary drops it and the caller's
        // standalone fallback cannot double-open.
        sock.abort();
        return ForwardResult::NoAck;
    }
    sock.disconnectFromServer();
    if (sock.state() != QLocalSocket::UnconnectedState)
        sock.waitForDisconnected(500);
    return ForwardResult::Acked;
}

void SingleInstance::ackClient(QLocalSocket *client) {
    if (!client) return;
    static constexpr char ack = kAckByte;
    client->write(&ack, 1);   // fails harmlessly if peer already hung up
    client->flush();
}

#ifdef Q_OS_WIN
void *SingleInstance::acquireSingletonMutex(const QString &serverName,
                                            bool *alreadyExists) {
    const QString full = QStringLiteral("Local\\") + serverName;
    ::SetLastError(ERROR_SUCCESS);
    HANDLE h = ::CreateMutexW(nullptr, FALSE,
                              reinterpret_cast<const wchar_t *>(full.utf16()));
    const DWORD err = ::GetLastError();
    *alreadyExists = (err == ERROR_ALREADY_EXISTS) ||
                     (!h && err == ERROR_ACCESS_DENIED);
    return h;
}
#endif
