// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_CURL_CONVERT_H
#define NOTEPATRA_CURL_CONVERT_H

// Pure curl <-> request conversion for the REST client (no widgets).
//
// Import understands POSIX shell quoting ('...', "...", $'...', backslash
// continuations) and Windows cmd quoting (^ escapes and ^ continuations, as
// in a browser's "Copy as cURL (cmd)"). It never reads files (-d @file,
// --data-urlencode name@file, -b cookiefile are reported in `notes`) and it
// never turns TLS verification off (-k is reported, not applied).

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace CurlConvert {

struct Request {
    QString method = QStringLiteral("GET");
    QString url;
    QList<QPair<QString, QString>> headers;
    QString body;
};

struct ImportResult {
    bool ok = false;
    QString error;      // set when ok == false
    Request request;
    QStringList notes;  // things that were ignored or not applied
};

ImportResult fromCurl(const QString &command);

// A POSIX-shell-safe command: every argument single-quoted, embedded single
// quotes written as '\'' . Multi-line with " \" continuations.
QString toCurl(const Request &request);

// Shell-style split, exposed for tests.
QStringList tokenize(const QString &command, QString *error = nullptr);

}  // namespace CurlConvert

#endif  // NOTEPATRA_CURL_CONVERT_H
