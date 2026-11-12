// SPDX-License-Identifier: GPL-3.0-or-later

#include "rest_history.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace RestHistory {

namespace {

const QString kMarker = QStringLiteral("<redacted>");

bool sensitiveName(const QString &lower) {
    static const char *const parts[] = {"token", "secret", "password", "passwd", "apikey",
                                        "api-key", "api_key", "auth", "session", "credential",
                                        "signature", "cookie"};
    for (const char *p : parts)
        if (lower.contains(QLatin1String(p))) return true;
    return lower == QLatin1String("key") || lower == QLatin1String("sig") ||
           lower == QLatin1String("code");
}

// True when `value` is nothing but {{variables}}, optionally after an auth
// scheme word ("Bearer {{token}}"). Anything literal left over may be a secret.
bool onlyVariables(const QString &value) {
    static const QRegularExpression ref(QStringLiteral("\\{\\{[^{}]*\\}\\}"));
    static const QRegularExpression scheme(
        QStringLiteral("^\\s*(Bearer|Basic|Token|Digest|Negotiate|ApiKey|Key)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    if (!value.contains(ref)) return false;
    QString rest = value;
    rest.remove(ref);
    rest.remove(scheme);
    return rest.trimmed().isEmpty();
}

QJsonObject readRoot(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
    return d.isObject() ? d.object() : QJsonObject();
}

bool writeRoot(const QString &path, QJsonObject root, QString *error) {
    root.insert(QStringLiteral("version"), 1);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
#ifndef Q_OS_WIN
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);  // may hold URLs/bodies
#endif
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

QJsonObject toJson(const Entry &e) {
    QJsonObject o;
    o.insert(QStringLiteral("time"), e.time.toUTC().toString(Qt::ISODate));
    o.insert(QStringLiteral("method"), e.method);
    o.insert(QStringLiteral("url"), e.url);
    o.insert(QStringLiteral("headers"), e.headers);
    o.insert(QStringLiteral("body"), e.body);
    if (e.bodyOmitted) o.insert(QStringLiteral("bodyOmitted"), true);
    o.insert(QStringLiteral("status"), e.status);
    if (!e.outcome.isEmpty()) o.insert(QStringLiteral("outcome"), e.outcome);
    o.insert(QStringLiteral("ms"), double(e.ms));
    o.insert(QStringLiteral("size"), double(e.size));
    if (!e.name.isEmpty()) o.insert(QStringLiteral("name"), e.name);
    if (!e.sourceFile.isEmpty()) o.insert(QStringLiteral("file"), e.sourceFile);
    return o;
}

Entry fromJson(const QJsonObject &o) {
    Entry e;
    e.time = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODate);
    e.method = o.value(QStringLiteral("method")).toString();
    e.url = o.value(QStringLiteral("url")).toString();
    e.headers = o.value(QStringLiteral("headers")).toString();
    e.body = o.value(QStringLiteral("body")).toString();
    e.bodyOmitted = o.value(QStringLiteral("bodyOmitted")).toBool();
    e.status = o.value(QStringLiteral("status")).toInt();
    e.outcome = o.value(QStringLiteral("outcome")).toString();
    e.ms = qint64(o.value(QStringLiteral("ms")).toDouble());
    e.size = qint64(o.value(QStringLiteral("size")).toDouble());
    e.name = o.value(QStringLiteral("name")).toString();
    e.sourceFile = o.value(QStringLiteral("file")).toString();
    return e;
}

}  // namespace

QString redactedMarker() { return kMarker; }

bool isSensitiveHeader(const QString &name) {
    const QString l = name.trimmed().toLower();
    return l == QLatin1String("authorization") || l == QLatin1String("proxy-authorization") ||
           sensitiveName(l);
}

QString redactHeaderValue(const QString &name, const QString &value) {
    if (!isSensitiveHeader(name) || value.trimmed().isEmpty() || onlyVariables(value))
        return value;
    return kMarker;
}

QString redactHeaders(const QString &headersText) {
    QStringList out;
    for (const QString &line : headersText.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0) {
            out << line;
            continue;
        }
        const QString name = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();
        const QString red = redactHeaderValue(name, value);
        out << (red == value ? line : name + QStringLiteral(": ") + red);
    }
    return out.join(QLatin1Char('\n'));
}

QString redactUrl(const QString &url) {
    QString u = url;
    // user:password@ in the authority
    static const QRegularExpression userinfo(
        QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]*://)([^/?#@:]*):([^/?#@]*)@"));
    const QRegularExpressionMatch m = userinfo.match(u);
    if (m.hasMatch() && !m.captured(3).isEmpty() && !onlyVariables(m.captured(3)))
        u.replace(m.capturedStart(3), m.capturedLength(3), kMarker);

    const int q = u.indexOf(QLatin1Char('?'));
    if (q < 0) return u;
    int fragment = u.indexOf(QLatin1Char('#'), q);
    if (fragment < 0) fragment = u.size();
    QStringList items = u.mid(q + 1, fragment - q - 1).split(QLatin1Char('&'));
    for (QString &it : items) {
        const int eq = it.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        const QString name = it.left(eq);
        const QString value = it.mid(eq + 1);
        if (value.isEmpty() || onlyVariables(value)) continue;
        if (sensitiveName(name.toLower())) it = name + QLatin1Char('=') + kMarker;
    }
    return u.left(q + 1) + items.join(QLatin1Char('&')) + u.mid(fragment);
}

bool containsRedacted(const QString &text) { return text.contains(kMarker); }

Entry redacted(const Entry &in) {
    Entry e = in;
    e.url = redactUrl(e.url);
    e.headers = redactHeaders(e.headers);
    if (e.body.size() > kMaxStoredBody) {
        e.body.clear();
        e.bodyOmitted = true;
    }
    return e;
}

QList<Entry> load(const QString &path) {
    QList<Entry> out;
    const QJsonArray arr = readRoot(path).value(QStringLiteral("history")).toArray();
    for (const QJsonValue &v : arr) {
        if (out.size() >= kMaxEntries) break;
        if (v.isObject()) out << fromJson(v.toObject());
    }
    return out;
}

bool append(const QString &path, const Entry &e, QString *error) {
    QJsonObject root = readRoot(path);
    QJsonArray arr = root.value(QStringLiteral("history")).toArray();
    arr.prepend(toJson(redacted(e)));
    while (arr.size() > kMaxEntries) arr.removeLast();
    root.insert(QStringLiteral("history"), arr);
    return writeRoot(path, root, error);
}

bool clear(const QString &path, QString *error) {
    QJsonObject root = readRoot(path);
    root.insert(QStringLiteral("history"), QJsonArray());
    return writeRoot(path, root, error);
}

QString environmentFor(const QString &path, const QString &dir) {
    return readRoot(path).value(QStringLiteral("environments")).toObject().value(dir).toString();
}

bool setEnvironmentFor(const QString &path, const QString &dir, const QString &name) {
    if (dir.isEmpty()) return false;
    QJsonObject root = readRoot(path);
    QJsonObject envs = root.value(QStringLiteral("environments")).toObject();
    if (envs.value(dir).toString() == name) return true;
    if (name.isEmpty()) envs.remove(dir);
    else envs.insert(dir, name);
    root.insert(QStringLiteral("environments"), envs);
    return writeRoot(path, root, nullptr);
}

}  // namespace RestHistory
