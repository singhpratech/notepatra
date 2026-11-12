// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_REST_HISTORY_H
#define NOTEPATRA_REST_HISTORY_H

// REST client history + per-folder environment choice, kept in one JSON
// file in the per-user config dir. Written with QSaveFile (atomic) and
// owner-only permissions on Unix. Requests are stored with {{variables}}
// unexpanded, and literal secrets in Authorization / Cookie / API-key style
// headers, URL user-info and token-like query parameters are replaced by
// "<redacted>" before anything touches the disk.

#include <QDateTime>
#include <QList>
#include <QString>

namespace RestHistory {

constexpr int kMaxEntries = 50;
constexpr int kMaxStoredBody = 256 * 1024;

QString redactedMarker();  // "<redacted>"

struct Entry {
    QDateTime time;
    QString method;
    QString url;       // unexpanded, redacted
    QString headers;   // "Name: value" lines, unexpanded, redacted
    QString body;      // unexpanded; empty when bodyOmitted
    bool bodyOmitted = false;
    int status = 0;    // 0 = no HTTP response (network error / cancelled)
    QString outcome;   // "Cancelled", "Timed out", error text; empty on a response
    qint64 ms = 0;
    qint64 size = 0;
    QString name;      // request name from the .http file
    QString sourceFile;
};

bool isSensitiveHeader(const QString &name);
QString redactHeaderValue(const QString &name, const QString &value);
QString redactHeaders(const QString &headersText);
QString redactUrl(const QString &url);
bool containsRedacted(const QString &text);
Entry redacted(const Entry &e);

QList<Entry> load(const QString &path);
// Prepends `e` (redacted), keeps the newest kMaxEntries.
bool append(const QString &path, const Entry &e, QString *error = nullptr);
bool clear(const QString &path, QString *error = nullptr);

QString environmentFor(const QString &path, const QString &dir);
bool setEnvironmentFor(const QString &path, const QString &dir, const QString &name);

}  // namespace RestHistory

#endif  // NOTEPATRA_REST_HISTORY_H
