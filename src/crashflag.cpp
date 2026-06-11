// SPDX-License-Identifier: GPL-3.0-or-later
#include "crashflag.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cstring>
#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#endif

// CreateFileW (not CreateFileA): same async-signal-safety, but an ANSI path
// would silently fail on a non-ASCII %APPDATA% (e.g. Cyrillic username) —
// the exact CP_ACP bug class the argv fix removes.
#ifdef Q_OS_WIN
static wchar_t g_flagPath[1024] = {0};
#else
static char g_flagPath[1024] = {0};
#endif

void crashFlagInit(const QString &flagPath) {
    QDir().mkpath(QFileInfo(flagPath).path());
#ifdef Q_OS_WIN
    g_flagPath[0] = 0;
    if (flagPath.size() < 1023) {
        const int n = QDir::toNativeSeparators(flagPath).toWCharArray(g_flagPath);
        g_flagPath[n] = 0;
    }
#else
    g_flagPath[0] = 0;
    const QByteArray native = QFile::encodeName(flagPath);
    if (native.size() < 1023) {
        memcpy(g_flagPath, native.constData(), size_t(native.size()) + 1);
    }
#endif
}

void crashFlagWrite() {
    if (!g_flagPath[0]) return;
#ifdef Q_OS_WIN
    HANDLE h = CreateFileW(g_flagPath, GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD n = 0;
        WriteFile(h, "crashed", 7, &n, nullptr);
        CloseHandle(h);
    }
#else
    const int fd = ::open(g_flagPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        const ssize_t r = ::write(fd, "crashed", 7);
        (void)r;
        ::close(fd);
    }
#endif
}
