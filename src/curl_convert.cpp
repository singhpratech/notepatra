// SPDX-License-Identifier: GPL-3.0-or-later

#include "curl_convert.h"

#include <QHash>
#include <QRegularExpression>
#include <QUrl>

namespace CurlConvert {

namespace {

bool isBlank(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') ||
           c == QLatin1Char('\r');
}

int hexValue(QChar c) {
    const ushort u = c.unicode();
    if (u >= '0' && u <= '9') return u - '0';
    if (u >= 'a' && u <= 'f') return u - 'a' + 10;
    if (u >= 'A' && u <= 'F') return u - 'A' + 10;
    return -1;
}

// Body of $'...' starting just after the opening quote. Returns the index of
// the closing quote, or -1.
int readAnsiC(const QString &s, int i, QString *out) {
    const int n = s.size();
    while (i < n) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\'')) return i;
        if (c != QLatin1Char('\\') || i + 1 >= n) {
            *out += c;
            ++i;
            continue;
        }
        const QChar e = s.at(i + 1);
        i += 2;
        switch (e.unicode()) {
        case 'n': *out += QLatin1Char('\n'); break;
        case 't': *out += QLatin1Char('\t'); break;
        case 'r': *out += QLatin1Char('\r'); break;
        case 'a': *out += QChar(7); break;
        case 'b': *out += QChar(8); break;
        case 'e': case 'E': *out += QChar(27); break;
        case 'f': *out += QChar(12); break;
        case 'v': *out += QChar(11); break;
        case 'x': case 'u': case 'U': {
            const int maxDigits = e == QLatin1Char('x') ? 2 : e == QLatin1Char('u') ? 4 : 8;
            uint v = 0;
            int d = 0;
            while (d < maxDigits && i < n && hexValue(s.at(i)) >= 0) {
                v = v * 16 + uint(hexValue(s.at(i)));
                ++i;
                ++d;
            }
            if (d == 0) {
                *out += QLatin1Char('\\');
                *out += e;
            } else if (e == QLatin1Char('x')) {
                *out += QChar(ushort(v));  // byte value as Latin-1
            } else {
                *out += QString::fromUcs4(&v, 1);
            }
            break;
        }
        default:
            if (e >= QLatin1Char('0') && e <= QLatin1Char('7')) {
                uint v = uint(e.unicode() - '0');
                int d = 1;
                while (d < 3 && i < n && s.at(i) >= QLatin1Char('0') && s.at(i) <= QLatin1Char('7')) {
                    v = v * 8 + uint(s.at(i).unicode() - '0');
                    ++i;
                    ++d;
                }
                *out += QChar(ushort(v & 0xFF));
            } else {
                *out += e;  // \\ \' \" \? and anything else
            }
        }
    }
    return -1;
}

QStringList tokenizePosix(const QString &s, QString *error) {
    QStringList args;
    QString cur;
    bool inArg = false;
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 < n && s.at(i + 1) == QLatin1Char('\n')) { ++i; continue; }
            if (i + 2 < n && s.at(i + 1) == QLatin1Char('\r') && s.at(i + 2) == QLatin1Char('\n')) {
                i += 2;
                continue;
            }
            if (i + 1 < n) { cur += s.at(++i); inArg = true; }
            continue;
        }
        if (c == QLatin1Char('\'')) {
            const int close = s.indexOf(QLatin1Char('\''), i + 1);
            if (close < 0) { if (error) *error = QStringLiteral("Unterminated ' quote."); return {}; }
            cur += s.midRef(i + 1, close - i - 1);
            i = close;
            inArg = true;
            continue;
        }
        if (c == QLatin1Char('$') && i + 1 < n && s.at(i + 1) == QLatin1Char('\'')) {
            const int close = readAnsiC(s, i + 2, &cur);
            if (close < 0) { if (error) *error = QStringLiteral("Unterminated $' quote."); return {}; }
            i = close;
            inArg = true;
            continue;
        }
        if (c == QLatin1Char('"')) {
            int j = i + 1;
            bool closed = false;
            for (; j < n; ++j) {
                const QChar d = s.at(j);
                if (d == QLatin1Char('"')) { closed = true; break; }
                if (d == QLatin1Char('\\') && j + 1 < n) {
                    const QChar e = s.at(j + 1);
                    if (e == QLatin1Char('\n')) { ++j; continue; }
                    if (e == QLatin1Char('"') || e == QLatin1Char('\\') || e == QLatin1Char('$') ||
                        e == QLatin1Char('`')) {
                        cur += e;
                        ++j;
                        continue;
                    }
                }
                cur += d;
            }
            if (!closed) { if (error) *error = QStringLiteral("Unterminated \" quote."); return {}; }
            i = j;
            inArg = true;
            continue;
        }
        if (isBlank(c)) {
            if (inArg) { args << cur; cur.clear(); inArg = false; }
            continue;
        }
        cur += c;
        inArg = true;
    }
    if (inArg) args << cur;
    return args;
}

// Windows: first the cmd.exe layer (^ escapes, ^ line continuations), then
// the MSVCRT argv rules ("..." quoting, \" literal quotes).
QStringList tokenizeCmd(const QString &in) {
    QString s;
    bool cmdQuoted = false;
    const int n = in.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = in.at(i);
        if (c == QLatin1Char('"')) { cmdQuoted = !cmdQuoted; s += c; continue; }
        if (c == QLatin1Char('^') && !cmdQuoted) {
            if (i + 1 >= n) continue;
            const QChar nx = in.at(i + 1);
            if (nx == QLatin1Char('\r') && i + 2 < n && in.at(i + 2) == QLatin1Char('\n')) {
                s += QLatin1Char(' ');
                i += 2;
            } else if (nx == QLatin1Char('\n')) {
                s += QLatin1Char(' ');
                ++i;
            } else {
                s += nx;
                ++i;
            }
            continue;
        }
        s += c;
    }

    QStringList args;
    QString cur;
    bool inArg = false, quoted = false;
    const int m = s.size();
    for (int i = 0; i < m; ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\')) {
            int k = i;
            while (k < m && s.at(k) == QLatin1Char('\\')) ++k;
            const int count = k - i;
            if (k < m && s.at(k) == QLatin1Char('"')) {
                cur += QString(count / 2, QLatin1Char('\\'));
                if (count % 2) cur += QLatin1Char('"');
                else quoted = !quoted;
                i = k;
            } else {
                cur += QString(count, QLatin1Char('\\'));
                i = k - 1;
            }
            inArg = true;
            continue;
        }
        if (c == QLatin1Char('"')) { quoted = !quoted; inArg = true; continue; }
        if (isBlank(c) && !quoted) {
            if (inArg) { args << cur; cur.clear(); inArg = false; }
            continue;
        }
        cur += c;
        inArg = true;
    }
    if (inArg) args << cur;
    return args;
}

QString shellQuote(const QString &s) {
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

bool hasHeader(const QList<QPair<QString, QString>> &h, const QString &name) {
    for (const auto &p : h)
        if (p.first.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

}  // namespace

QStringList tokenize(const QString &command, QString *error) {
    // Windows cmd: a ^ line continuation, or the whole command starting curl ^"
    static const QRegularExpression cmdHint(
        QStringLiteral("\\^\\r?\\n|^\\s*curl(\\.exe)?\\s+\\^\""),
        QRegularExpression::CaseInsensitiveOption);
    if (command.contains(cmdHint)) return tokenizeCmd(command);
    return tokenizePosix(command, error);
}

ImportResult fromCurl(const QString &command) {
    ImportResult r;
    QString err;
    const QStringList t = tokenize(command.trimmed(), &err);
    if (!err.isEmpty()) {
        r.error = err;
        return r;
    }
    const QString prog = t.value(0).toLower();
    if (t.isEmpty() || !(prog == QLatin1String("curl") || prog == QLatin1String("curl.exe") ||
                         prog.endsWith(QLatin1String("/curl")) ||
                         prog.endsWith(QLatin1String("\\curl.exe")))) {
        r.error = QStringLiteral("Not a curl command (it must start with curl).");
        return r;
    }

    QString method, url;
    QStringList data;
    QStringList cookies;
    bool getMode = false, head = false, json = false;
    auto &headers = r.request.headers;

    static const QStringList longWithArg = {
        "output", "max-time", "connect-timeout", "write-out", "cacert", "capath", "cert",
        "key", "cert-type", "key-type", "proxy", "proxy-user", "retry", "retry-delay",
        "retry-max-time", "upload-file", "range", "resolve", "limit-rate", "cookie-jar",
        "config", "interface", "max-redirs", "proto", "proto-redir", "tls-max", "ciphers",
        "dns-servers", "speed-limit", "speed-time", "time-cond", "continue-at", "form",
        "form-string", "connect-to", "pinnedpubkey", "variable", "expand-url", "trace",
        "trace-ascii", "stderr", "dump-header", "unix-socket", "abstract-unix-socket"};
    static const QString shortWithArg = QStringLiteral("XHdubAeomwExUTrcKYyzCFD");

    auto handle = [&](const QString &opt, const QString &val) -> void {
        if (opt == "request") method = val.toUpper();
        else if (opt == "header") {
            const int colon = val.indexOf(QLatin1Char(':'));
            if (colon > 0) {
                const QString v = val.mid(colon + 1).trimmed();
                if (v.isEmpty()) r.notes << QStringLiteral("Ignored -H '%1' (curl uses it to remove a header).").arg(val);
                else headers.append({val.left(colon).trimmed(), v});
            } else if (val.endsWith(QLatin1Char(';'))) {
                headers.append({val.left(val.size() - 1).trimmed(), QString()});
            } else {
                r.notes << QStringLiteral("Ignored malformed header: %1").arg(val);
            }
        } else if (opt == "data" || opt == "data-binary") {
            if (val.startsWith(QLatin1Char('@'))) {
                r.notes << QStringLiteral("Body from file %1 was not read; paste the body into the Body tab.").arg(val);
            } else {
                data << val;
            }
        } else if (opt == "data-raw") {
            data << val;
        } else if (opt == "data-urlencode") {
            int p = val.indexOf(QLatin1Char('='));
            if (p < 0) p = val.indexOf(QLatin1Char('@'));
            if (p >= 0 && val.at(p) == QLatin1Char('@')) {
                r.notes << QStringLiteral("--data-urlencode %1 reads a file; it was not read.").arg(val);
            } else if (p < 0) {
                data << QString::fromLatin1(QUrl::toPercentEncoding(val));
            } else {
                const QString name = val.left(p);
                const QString enc = QString::fromLatin1(QUrl::toPercentEncoding(val.mid(p + 1)));
                data << (name.isEmpty() ? enc : name + QLatin1Char('=') + enc);
            }
        } else if (opt == "json") {
            if (val.startsWith(QLatin1Char('@'))) {
                r.notes << QStringLiteral("--json %1 reads a file; it was not read.").arg(val);
            } else {
                json = true;
                r.request.body += val;
            }
        } else if (opt == "user") {
            QString cred = val;
            if (!cred.contains(QLatin1Char(':'))) {
                r.notes << QStringLiteral("-u without a password: the Authorization header uses an empty password.");
                cred += QLatin1Char(':');
            }
            headers.append({QStringLiteral("Authorization"),
                            QStringLiteral("Basic ") + QString::fromLatin1(cred.toUtf8().toBase64())});
        } else if (opt == "oauth2-bearer") {
            headers.append({QStringLiteral("Authorization"), QStringLiteral("Bearer ") + val});
        } else if (opt == "cookie") {
            if (val.contains(QLatin1Char('='))) cookies << val;
            else r.notes << QStringLiteral("Cookie file %1 was not read.").arg(val);
        } else if (opt == "user-agent") {
            headers.append({QStringLiteral("User-Agent"), val});
        } else if (opt == "referer") {
            headers.append({QStringLiteral("Referer"), val});
        } else if (opt == "url") {
            if (url.isEmpty()) url = val;
        } else if (opt == "url-query") {
            url += (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + val;
        } else if (opt == "get") {
            getMode = true;
        } else if (opt == "head") {
            head = true;
        } else if (opt == "insecure") {
            r.notes << QStringLiteral("-k ignored: TLS certificate checks stay on.");
        } else if (opt == "location") {
            r.notes << QStringLiteral("-L noted: the REST client shows redirects instead of following them.");
        } else if (opt == "form" || opt == "form-string") {
            r.notes << QStringLiteral("-F (multipart form) is not supported; that part was skipped.");
        }
        // Everything else (--compressed, -s, -v, -o, ...) is ignored on purpose.
    };

    static const QHash<QString, QString> shortMap = {
        {"X", "request"}, {"H", "header"}, {"d", "data"}, {"u", "user"}, {"b", "cookie"},
        {"A", "user-agent"}, {"e", "referer"}, {"G", "get"}, {"I", "head"}, {"k", "insecure"},
        {"L", "location"}, {"F", "form"}};
    static const QHash<QString, QString> longAlias = {
        {"data-ascii", "data"}, {"location-trusted", "location"}};
    static const QStringList longWithValue = {
        "request", "header", "data", "data-ascii", "data-raw", "data-binary", "data-urlencode",
        "json", "user", "oauth2-bearer", "cookie", "user-agent", "referer", "url", "url-query"};

    bool endOfOptions = false;
    for (int i = 1; i < t.size(); ++i) {
        const QString &a = t.at(i);
        if (!endOfOptions && a == QLatin1String("--")) { endOfOptions = true; continue; }
        if (endOfOptions || !a.startsWith(QLatin1Char('-')) || a == QLatin1String("-")) {
            if (url.isEmpty()) url = a;
            else r.notes << QStringLiteral("Ignored extra URL: %1").arg(a);
            continue;
        }
        if (a.startsWith(QLatin1String("--"))) {
            QString name = a.mid(2);
            name = longAlias.value(name, name);
            if (longWithValue.contains(name) || longWithArg.contains(name)) {
                if (i + 1 >= t.size()) {
                    r.error = QStringLiteral("Option %1 needs a value.").arg(a);
                    return r;
                }
                handle(name, t.at(++i));
            } else {
                handle(name, QString());
            }
            continue;
        }
        for (int j = 1; j < a.size(); ++j) {
            const QString ch = a.mid(j, 1);
            if (shortWithArg.contains(ch)) {
                QString val = a.mid(j + 1);
                if (val.isEmpty()) {
                    if (i + 1 >= t.size()) {
                        r.error = QStringLiteral("Option -%1 needs a value.").arg(ch);
                        return r;
                    }
                    val = t.at(++i);
                }
                handle(shortMap.value(ch), val);
                break;
            }
            handle(shortMap.value(ch), QString());
        }
    }

    if (url.isEmpty()) {
        r.error = QStringLiteral("No URL found in the curl command.");
        return r;
    }
    if (!cookies.isEmpty()) headers.append({QStringLiteral("Cookie"), cookies.join(QStringLiteral("; "))});
    const QString joined = data.join(QLatin1Char('&'));
    if (getMode && !joined.isEmpty()) {
        url += (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + joined;
    } else if (!joined.isEmpty()) {
        r.request.body = r.request.body.isEmpty() ? joined : r.request.body + QLatin1Char('&') + joined;
    }
    if (json) {
        if (!hasHeader(headers, QStringLiteral("Content-Type")))
            headers.append({QStringLiteral("Content-Type"), QStringLiteral("application/json")});
        if (!hasHeader(headers, QStringLiteral("Accept")))
            headers.append({QStringLiteral("Accept"), QStringLiteral("application/json")});
    }
    const bool hasBody = !r.request.body.isEmpty();
    if (!method.isEmpty()) r.request.method = method;
    else if (head) r.request.method = QStringLiteral("HEAD");
    else if (hasBody && !getMode) r.request.method = QStringLiteral("POST");
    else r.request.method = QStringLiteral("GET");
    r.request.url = url;
    r.ok = true;
    return r;
}

QString toCurl(const Request &req) {
    QStringList parts;
    const QString m = req.method.trimmed().toUpper();
    QString first = QStringLiteral("curl");
    if (m == QLatin1String("HEAD") && req.body.isEmpty()) first += QStringLiteral(" --head");
    else if (!(m == QLatin1String("GET") && req.body.isEmpty()) && !m.isEmpty())
        first += QStringLiteral(" -X ") +
                 (m.contains(QRegularExpression(QStringLiteral("^[A-Z]+$"))) ? m : shellQuote(m));
    first += QLatin1Char(' ') + shellQuote(req.url);
    parts << first;
    for (const auto &h : req.headers) {
        const QString line = h.second.isEmpty() ? h.first + QLatin1Char(';')
                                                : h.first + QStringLiteral(": ") + h.second;
        parts << QStringLiteral("  -H ") + shellQuote(line);
    }
    if (!req.body.isEmpty()) parts << QStringLiteral("  --data-raw ") + shellQuote(req.body);
    return parts.join(QStringLiteral(" \\\n"));
}

}  // namespace CurlConvert
