// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_REST_HISTORY_H
#define NOTEPATRA_REST_HISTORY_H

// REST client history + per-folder environment choice, kept in one JSON
// file in the per-user config dir. Written with QSaveFile (atomic) and
// owner-only permissions on Unix. Requests are stored with {{variables}}
// unexpanded, and literal secrets are replaced by "<redacted>" before
// anything touches the disk: Authorization / Cookie / API-key style headers
// (any header name containing "key"), URL user-info, token-like query
// parameters, and secret-named fields of JSON and form-urlencoded bodies.
// Any other body is kept only when it is small text; otherwise it is stored
// as "<body omitted: N bytes, type>".

#include <QDateTime>
#include <QList>
#include <QString>

namespace RestHistory {

constexpr int kMaxEntries = 50;
constexpr int kMaxStoredBody = 256 * 1024;
// A body that is neither JSON nor form-urlencoded is kept only up to this.
constexpr int kMaxStoredOtherBody = 4 * 1024;

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
// True for a field name whose value is a secret in a JSON or form body:
// case-insensitive, '-', '_', '.' and spaces ignored.
bool isSensitiveField(const QString &name);
// The body as history stores it (headersText gives its Content-Type).
// *omitted is set when the whole body was replaced by the omitted marker.
QString redactBody(const QString &body, const QString &headersText, bool *omitted = nullptr);
// True when a body still holds a <redacted> value or the omitted marker:
// Send refuses it, as it refuses a <redacted> URL or header.
bool bodyBlocksSend(const QString &body);
Entry redacted(const Entry &e);

QList<Entry> load(const QString &path);
// Prepends `e` (redacted), keeps the newest kMaxEntries.
bool append(const QString &path, const Entry &e, QString *error = nullptr);
bool clear(const QString &path, QString *error = nullptr);

QString environmentFor(const QString &path, const QString &dir);
bool setEnvironmentFor(const QString &path, const QString &dir, const QString &name);

}  // namespace RestHistory

#endif  // NOTEPATRA_REST_HISTORY_H
