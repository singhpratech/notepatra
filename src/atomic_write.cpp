// SPDX-License-Identifier: GPL-3.0-or-later

#include "atomic_write.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>

// <windows.h> is confined to THIS translation unit on purpose — see the
// header's closing note. WIN32_LEAN_AND_MEAN and NOMINMAX keep even this
// TU's macro exposure to a minimum.
#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <io.h>          // _get_osfhandle
#else
#  include <cerrno>
#  include <cstdio>        // std::rename
#  include <cstring>       // strerror
#  include <fcntl.h>       // open(O_RDONLY) for the directory fsync
#  include <unistd.h>      // fsync, close
#endif

namespace NpAtomic {

bool syncData(QFile &file) {
    file.flush();
    const int fd = file.handle();
    if (fd < 0) return false;
#ifdef Q_OS_WIN
    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE) return false;
    return ::FlushFileBuffers(h) != 0;
#else
    return ::fsync(fd) == 0;
#endif
}

void syncDirectoryOf(const QString &path) {
#ifdef Q_OS_WIN
    Q_UNUSED(path);
#else
    const QByteArray dirPath = QFileInfo(path).absolutePath().toLocal8Bit();
    if (dirPath.isEmpty()) return;
    const int dfd = ::open(dirPath.constData(), O_RDONLY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
#endif
}

bool replaceFile(const QString &tmpPath,
                 const QString &target,
                 QString *errorOut) {
#ifndef Q_OS_WIN
    // Keep the target's mode bits (an executable script stays +x).
    if (QFileInfo(target).isFile())
        QFile::setPermissions(tmpPath, QFile::permissions(target));
#endif
#ifdef Q_OS_WIN
    // Bind to named locals: utf16() points into the QString, which must
    // outlive the call.
    const QString nativeTmp    = QDir::toNativeSeparators(tmpPath);
    const QString nativeTarget = QDir::toNativeSeparators(target);
    if (!::MoveFileExW(reinterpret_cast<const wchar_t *>(nativeTmp.utf16()),
                       reinterpret_cast<const wchar_t *>(nativeTarget.utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const unsigned long err = ::GetLastError();
        QFile::remove(tmpPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not replace \"%1\" (Windows error %2). "
                                       "The existing file was left unchanged.")
                            .arg(nativeTarget)
                            .arg(static_cast<qulonglong>(err));
        return false;
    }
#else
    if (std::rename(QFile::encodeName(tmpPath).constData(),
                    QFile::encodeName(target).constData()) != 0) {
        const int err = errno;
        QFile::remove(tmpPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not replace \"%1\": %2. "
                                       "The existing file was left unchanged.")
                            .arg(target, QString::fromLocal8Bit(::strerror(err)));
        return false;
    }
#endif
    syncDirectoryOf(target);
    return true;
}

bool writeFileNoFollow(const QString &target, const QByteArray &bytes, QString *errorOut) {
    QFile tmp;
    bool opened = false;
    for (int attempt = 0; attempt < 16 && !opened; ++attempt) {
        tmp.setFileName(target + QLatin1Char('.')
                        + QString::number(QRandomGenerator::global()->generate64(), 36)
                        + QStringLiteral(".tmp"));
        // NewOnly = O_CREAT|O_EXCL: an existing file or (even dangling)
        // symlink at the name fails the open instead of being written through.
        opened = tmp.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        if (!opened && !QFileInfo(tmp.fileName()).isSymLink() && !QFileInfo::exists(tmp.fileName()))
            break;  // a real error, not a name collision
    }
    if (!opened) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to open temp file: %1").arg(tmp.errorString());
        return false;
    }
    const qint64 n = tmp.write(bytes);
    syncData(tmp);
    tmp.close();
    if (n != bytes.size()) {
        QFile::remove(tmp.fileName());
        if (errorOut) *errorOut = QStringLiteral("Short write to temp file");
        return false;
    }
    return replaceFile(tmp.fileName(), target, errorOut);
}

}   // namespace NpAtomic
