// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QStringList>

struct CliArgs {
    QStringList files;     // non-flag args that resolved to real files (absolute)
    QStringList notFound;  // non-flag args that did NOT resolve (deleted, dir, typo)
    int gotoLine = -1;
    QString theme;
    bool newWindow = false;
};

// args = QCoreApplication::arguments(); index 0 (program name) is skipped.
CliArgs parseCliArgs(const QStringList &args);
