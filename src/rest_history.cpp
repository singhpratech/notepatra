// SPDX-License-Identifier: GPL-3.0-or-later

#include "rest_history.h"

#include <QFile>
#include <QUrl>
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

// ── Body redaction ──────────────────────────────────────────────────────
// A small scanner rather than a JSON parse: it keeps the user's formatting
// and key order, and it works on .http templates that are not valid JSON
// until their {{variables}} are expanded ({"n": {{count}}}).

int endOfJsonString(const QString &s, int i) {  // s[i] == '"'; index after the closing quote
    const int n = s.size();
    for (int j = i + 1; j < n; ++j) {
        if (s.at(j) == QLatin1Char('\\')) ++j;
        else if (s.at(j) == QLatin1Char('"')) return j + 1;
    }
    return n;
}

int endOfJsonValue(const QString &s, int v) {
    const int n = s.size();
    if (v >= n) return n;
    if (s.at(v) == QLatin1Char('"')) return endOfJsonString(s, v);
    if (s.midRef(v, 2) == QLatin1String("{{")) {
        const int close = s.indexOf(QLatin1String("}}"), v + 2);
        return close < 0 ? n : close + 2;
    }
    if (s.at(v) == QLatin1Char('{') || s.at(v) == QLatin1Char('[')) {
        int depth = 0;
        for (int j = v; j < n; ++j) {
            const QChar c = s.at(j);
            if (c == QLatin1Char('"')) {
                j = endOfJsonString(s, j) - 1;
            } else if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
                ++depth;
            } else if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
                if (--depth == 0) return j + 1;
            }
        }
        return n;
    }
    int j = v;
    while (j < n && s.at(j) != QLatin1Char(',') && s.at(j) != QLatin1Char('}') &&
           s.at(j) != QLatin1Char(']') && !s.at(j).isSpace())
        ++j;
    return j;
}

QString redactJsonText(const QString &s) {
    static const QRegularExpression bareVar(QStringLiteral("^\\{\\{[^{}]*\\}\\}$"));
    QString out;
    out.reserve(s.size());
    const int n = s.size();
    int i = 0;
    while (i < n) {
        if (s.at(i) != QLatin1Char('"')) {
            out += s.at(i++);
            continue;
        }
        const int j = endOfJsonString(s, i);
        out += s.midRef(i, j - i);
        int k = j;
        while (k < n && s.at(k).isSpace()) ++k;
        const QString key = s.mid(i + 1, qMax(0, j - i - 2));
        if (k < n && s.at(k) == QLatin1Char(':') && isSensitiveField(key)) {
            int v = k + 1;
            while (v < n && s.at(v).isSpace()) ++v;
            out += s.midRef(j, v - j);  // whitespace, colon, whitespace
            const int ve = endOfJsonValue(s, v);
            const QString val = s.mid(v, ve - v);
            const bool isString = val.startsWith(QLatin1Char('"'));
            const QString inner = isString ? val.mid(1, qMax(0, val.size() - 2)) : val;
            const bool keep = val.isEmpty() || (isString && inner.isEmpty()) ||
                              bareVar.match(val).hasMatch() || (isString && onlyVariables(inner));
            out += keep ? val : QStringLiteral("\"") + kMarker + QLatin1Char('"');
            i = ve;
            continue;
        }
        i = j;
    }
    return out;
}

QString redactFormText(const QString &s) {
    QStringList items = s.split(QLatin1Char('&'));
    for (QString &it : items) {
        const int eq = it.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString name = it.left(eq);
        name.replace(QLatin1Char('+'), QLatin1Char(' '));
        name = QUrl::fromPercentEncoding(name.toUtf8());
        const QString value = it.mid(eq + 1);
        if (value.isEmpty() || onlyVariables(value) || !isSensitiveField(name)) continue;
        it = it.left(eq) + QLatin1Char('=') + kMarker;
    }
    return items.join(QLatin1Char('&'));
}

QString contentTypeOf(const QString &headersText) {
    for (const QString &line : headersText.split(QLatin1Char('\n'))) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon > 0 &&
            line.left(colon).trimmed().compare(QLatin1String("content-type"), Qt::CaseInsensitive) == 0)
            return line.mid(colon + 1).section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    }
    return QString();
}

bool isTextBody(const QString &body, const QString &type) {
    for (const QChar c : body) {
        const ushort u = c.unicode();
        if (u < 0x20 && u != '\t' && u != '\n' && u != '\r') return false;
    }
    if (type.isEmpty() || type.startsWith(QLatin1String("text/"))) return true;
    static const char *const textish[] = {"json", "xml", "javascript", "graphql", "yaml",
                                          "csv", "x-www-form-urlencoded", "x-ndjson"};
    for (const char *t : textish)
        if (type.contains(QLatin1String(t))) return true;
    return false;
}

QString omittedMarker(const QString &body, const QString &type) {
    return QStringLiteral("<body omitted: %1 bytes, %2>")
        .arg(body.toUtf8().size())
        .arg(type.isEmpty() ? QStringLiteral("unknown type") : type);
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
    // Any "...key" header (Ocp-Apim-Subscription-Key, x-functions-key,
    // X-API-Key, ...) carries a credential.
    return l == QLatin1String("authorization") || l == QLatin1String("proxy-authorization") ||
           l.contains(QLatin1String("key")) || sensitiveName(l);
}

bool isSensitiveField(const QString &name) {
    QString n = name.toLower();
    n.remove(QLatin1Char('-'));
    n.remove(QLatin1Char('_'));
    n.remove(QLatin1Char('.'));
    n.remove(QLatin1Char(' '));
    static const char *const exact[] = {"pass", "pwd", "pin", "pincode", "pinnumber", "otp",
                                        "pan", "cvv", "cvc", "cvv2", "cvc2", "csc"};
    for (const char *e : exact)
        if (n == QLatin1String(e)) return true;
    static const char *const parts[] = {
        "password", "passwd", "passphrase", "passcode", "secret", "token", "apikey",
        "authorization", "session", "signature", "privatekey", "cardnumber", "cardno",
        "ccnumber", "creditcard", "securitycode"};
    for (const char *p : parts)
        if (n.contains(QLatin1String(p))) return true;
    // One-time codes: "totp", "otpcode", "smsotp" (not "footprint").
    return n.startsWith(QLatin1String("otp")) || n.endsWith(QLatin1String("otp"));
}

QString redactBody(const QString &body, const QString &headersText, bool *omitted) {
    if (omitted) *omitted = false;
    if (body.trimmed().isEmpty()) return body;
    const QString type = contentTypeOf(headersText);
    const QString t = body.trimmed();
    QString out;
    if (!isTextBody(body, type)) {
        out.clear();
    } else if (type.contains(QLatin1String("json")) || t.startsWith(QLatin1Char('{')) ||
               t.startsWith(QLatin1Char('['))) {
        out = redactJsonText(body);
    } else {
        static const QRegularExpression form(
            QStringLiteral("^[^=&\\s]+=[^&\\s]*(&[^=&\\s]+=[^&\\s]*)*$"));
        if (type == QLatin1String("application/x-www-form-urlencoded") ||
            (type.isEmpty() && form.match(t).hasMatch()))
            out = redactFormText(body);
        else if (body.size() <= kMaxStoredOtherBody)
            out = body;  // small text of another kind is kept as typed
    }
    if (out.isEmpty() || out.size() > kMaxStoredBody) {
        if (omitted) *omitted = true;
        return omittedMarker(body, type);
    }
    return out;
}

bool bodyBlocksSend(const QString &body) {
    return containsRedacted(body) || body.trimmed().startsWith(QLatin1String("<body omitted:"));
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
    // User-info in the authority: everything before the LAST '@' (a
    // password may itself contain '@'). user:password keeps the user name;
    // user-info with no ':' is often a token used as the user name
    // (https://TOKEN@host), so all of it goes.
    static const QRegularExpression authority(
        QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*://([^/?#]*)"));
    const QRegularExpressionMatch m = authority.match(u);
    if (m.hasMatch()) {
        const QString auth = m.captured(1);
        const int at = auth.lastIndexOf(QLatin1Char('@'));
        if (at > 0) {
            const QString info = auth.left(at);
            const int colon = info.indexOf(QLatin1Char(':'));
            QString safe = info;
            if (colon >= 0) {
                const QString pass = info.mid(colon + 1);
                if (!pass.isEmpty() && !onlyVariables(pass)) safe = info.left(colon + 1) + kMarker;
            } else if (!onlyVariables(info)) {
                safe = kMarker;
            }
            u.replace(m.capturedStart(1), at, safe);
        }
    }

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
    // The body's own Content-Type decides how it is read (in.headers: the
    // header VALUES may be redacted above, never the Content-Type).
    bool omitted = false;
    e.body = redactBody(e.body, in.headers, &omitted);
    e.bodyOmitted = omitted || in.bodyOmitted;
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
