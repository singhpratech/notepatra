// SPDX-License-Identifier: GPL-3.0-or-later

#include "http_file.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRandomGenerator>
#include <QUuid>
#include <QVector>

namespace HttpFile {

namespace {

constexpr int kMaxDepth = 16;
constexpr qint64 kMaxEnvFileBytes = 1024 * 1024;

bool isSeparator(const QStringRef &line) { return line.startsWith(QLatin1String("###")); }

bool isComment(const QStringRef &trimmed) {
    return trimmed.startsWith(QLatin1Char('#')) || trimmed.startsWith(QLatin1String("//"));
}

// RFC 7230 token: the characters allowed in a header field name.
bool isToken(const QStringRef &s) {
    if (s.isEmpty()) return false;
    for (const QChar c : s) {
        const ushort u = c.unicode();
        if (u > 127) return false;
        if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) continue;
        if (!QByteArray("!#$%&'*+-.^_`|~").contains(char(u))) return false;
    }
    return true;
}

bool isMethod(const QString &tok) {
    static const QStringList known = {
        QStringLiteral("GET"),     QStringLiteral("POST"),     QStringLiteral("PUT"),
        QStringLiteral("DELETE"),  QStringLiteral("PATCH"),    QStringLiteral("HEAD"),
        QStringLiteral("OPTIONS"), QStringLiteral("TRACE"),    QStringLiteral("CONNECT"),
        QStringLiteral("PROPFIND"), QStringLiteral("PROPPATCH"), QStringLiteral("MKCOL"),
        QStringLiteral("COPY"),    QStringLiteral("MOVE"),     QStringLiteral("LOCK"),
        QStringLiteral("UNLOCK"),  QStringLiteral("REPORT"),   QStringLiteral("PURGE")};
    if (known.contains(tok, Qt::CaseInsensitive)) return true;
    if (tok.size() < 3) return false;
    for (const QChar c : tok)
        if (c < QLatin1Char('A') || c > QLatin1Char('Z')) return false;
    return true;  // any all-caps token, e.g. a custom verb
}

QString stripHttpVersion(const QString &url) {
    const int sp = url.lastIndexOf(QRegExp(QStringLiteral("\\s")));
    if (sp > 0 && url.midRef(sp + 1).startsWith(QLatin1String("HTTP/"), Qt::CaseInsensitive))
        return url.left(sp).trimmed();
    return url;
}

// "name = value" (the leading '@' already removed). Value may be empty.
bool parseVariable(const QStringRef &t, QString *name, QString *value) {
    const int eq = t.indexOf(QLatin1Char('='));
    if (eq <= 0) return false;
    const QString n = t.left(eq).trimmed().toString();
    if (n.isEmpty()) return false;
    for (const QChar c : n)
        if (c.isSpace()) return false;
    *name = n;
    *value = t.mid(eq + 1).trimmed().toString();
    return true;
}

void parseBlock(const QVector<QStringRef> &lines, int blockStart, int a, int b,
                const QString &sepName, Document *doc) {
    QString metaName;
    int k = a;
    for (; k <= b; ++k) {
        const QStringRef t = lines[k].trimmed();
        if (t.isEmpty()) continue;
        if (isComment(t)) {
            QStringRef c = t.startsWith(QLatin1Char('#')) ? t.mid(1) : t.mid(2);
            c = c.trimmed();
            if (c.startsWith(QLatin1String("@name")) && c.size() > 5 &&
                (c.at(5).isSpace() || c.at(5) == QLatin1Char('='))) {
                QStringRef rest = c.mid(5).trimmed();
                if (rest.startsWith(QLatin1Char('='))) rest = rest.mid(1).trimmed();
                metaName = rest.toString();
            }
            continue;
        }
        if (t.startsWith(QLatin1Char('@'))) {
            QString n, v;
            if (parseVariable(t.mid(1), &n, &v)) doc->variables.append({n, v});
            continue;
        }
        break;
    }
    if (k > b) return;  // only comments / variables in this block

    Request r;
    r.name = sepName.isEmpty() ? metaName : sepName;
    r.startLine = blockStart;
    r.requestLine = k;
    r.endLine = b;

    const QString reqLine = lines[k].trimmed().toString();
    const int sp = reqLine.indexOf(QRegExp(QStringLiteral("\\s")));
    const QString first = sp > 0 ? reqLine.left(sp) : reqLine;
    if (sp > 0 && isMethod(first)) {
        r.method = first.toUpper();
        r.url = reqLine.mid(sp + 1).trimmed();
    } else {
        r.method = QStringLiteral("GET");
        r.url = reqLine;
    }
    r.url = stripHttpVersion(r.url);
    ++k;

    // VS Code multi-line URL: "?a=1" / "&b=2" continuation lines.
    while (k <= b) {
        const QStringRef t = lines[k].trimmed();
        if (!t.startsWith(QLatin1Char('?')) && !t.startsWith(QLatin1Char('&'))) break;
        r.url += stripHttpVersion(t.toString());
        ++k;
    }

    for (; k <= b; ++k) {
        const QStringRef t = lines[k].trimmed();
        if (t.isEmpty()) {
            ++k;
            break;
        }
        if (isComment(t)) continue;
        const int colon = t.indexOf(QLatin1Char(':'));
        const QStringRef name = colon > 0 ? t.left(colon).trimmed() : QStringRef();
        if (colon <= 0 || !isToken(name)) break;  // missing blank line: body starts here
        r.headers.append({name.toString(), t.mid(colon + 1).trimmed().toString()});
    }

    int last = b;
    while (last >= k && lines[last].trimmed().isEmpty()) --last;
    if (last >= k) {
        QStringList body;
        body.reserve(last - k + 1);
        for (int i = k; i <= last; ++i) body << lines[i].toString();
        r.body = body.join(QLatin1Char('\n'));
        const QStringRef firstBody = lines[k].trimmed();
        if (firstBody.startsWith(QLatin1String("< ")))
            r.bodyFromFile = firstBody.mid(2).trimmed().toString();
    }
    doc->requests.append(r);
}

bool validName(const QString &n) {
    if (n.isEmpty()) return false;
    if (n.startsWith(QLatin1Char('$'))) return n.size() > 1;
    for (const QChar c : n) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') ||
            c == QLatin1Char('.'))
            continue;
        return false;
    }
    return true;
}

// Calls fn(open, close, name) for every {{name}} reference with a valid name.
template <typename Fn> void scanRefs(const QString &s, Fn fn) {
    int pos = 0;
    while (true) {
        int open = s.indexOf(QLatin1String("{{"), pos);
        if (open < 0) return;
        while (open + 2 < s.size() && s.at(open + 2) == QLatin1Char('{')) ++open;
        const int close = s.indexOf(QLatin1String("}}"), open + 2);
        if (close < 0) return;
        const QString name = s.mid(open + 2, close - open - 2).trimmed();
        if (validName(name)) fn(open, close + 2, name);
        pos = close + 2;
    }
}

void appendUnique(QStringList *list, const QString &v) {
    if (!list->contains(v)) list->append(v);
}

QString jsonScalar(const QJsonValue &v, bool *ok) {
    *ok = true;
    if (v.isString()) return v.toString();
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return v.toVariant().toString();
    *ok = false;
    return QString();
}

}  // namespace

int Document::requestAtLine(int line) const {
    for (int i = 0; i < requests.size(); ++i)
        if (line >= requests[i].startLine && line <= requests[i].endLine) return i;
    return -1;
}

Document parse(const QString &textIn) {
    Document doc;
    QString text = textIn;
    if (text.startsWith(QChar(0xFEFF))) text.remove(0, 1);
    QVector<QStringRef> lines = text.splitRef(QLatin1Char('\n'));
    for (QStringRef &l : lines)
        if (l.endsWith(QLatin1Char('\r'))) l.chop(1);

    const int n = lines.size();
    int i = 0;
    while (i < n) {
        const int blockStart = i;
        QString sepName;
        if (isSeparator(lines[i])) {
            sepName = lines[i].mid(3).trimmed().toString();
            ++i;
        }
        int j = i;
        while (j < n && !isSeparator(lines[j])) ++j;
        parseBlock(lines, blockStart, i, j - 1, sepName, &doc);
        i = j;
    }
    return doc;
}

QString headersToText(const HeaderList &headers) {
    QStringList out;
    for (const auto &h : headers) out << h.first + QStringLiteral(": ") + h.second;
    return out.join(QLatin1Char('\n'));
}

HeaderList headersFromText(const QString &text) {
    HeaderList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#')) || t.startsWith(QLatin1String("//")))
            continue;
        const int colon = t.indexOf(QLatin1Char(':'));
        if (colon <= 0) continue;
        out.append({t.left(colon).trimmed(), t.mid(colon + 1).trimmed()});
    }
    return out;
}

// ── Resolver ────────────────────────────────────────────────────────────

Resolver::Resolver(const QMap<QString, QString> &environment,
                   const QList<QPair<QString, QString>> &fileVariables)
    : m_env(environment) {
    for (const auto &v : fileVariables) m_file.insert(v.first, v.second);  // last wins
}

bool Resolver::builtinDynamic(const QString &name, const QStringList &args, QString *out) {
    if (name == QLatin1String("$timestamp")) {
        *out = QString::number(QDateTime::currentSecsSinceEpoch());
        return true;
    }
    if (name == QLatin1String("$isoTimestamp")) {
        *out = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        return true;
    }
    if (name == QLatin1String("$uuid") || name == QLatin1String("$guid")) {
        *out = QUuid::createUuid().toString(QUuid::WithoutBraces);
        return true;
    }
    if (name == QLatin1String("$randomInt")) {
        int lo = 0, hi = 1000;
        if (args.size() == 2) {
            bool ok1 = false, ok2 = false;
            lo = args[0].toInt(&ok1);
            hi = args[1].toInt(&ok2);
            if (!ok1 || !ok2 || lo >= hi) return false;
        } else if (!args.isEmpty()) {
            return false;
        }
        *out = QString::number(QRandomGenerator::global()->bounded(lo, hi));
        return true;
    }
    return false;  // $processEnv, $dotenv, ... are deliberately unsupported
}

bool Resolver::lookup(const QString &name, int depth, QStringList *stack, ResolveResult *acc,
                      QString *out) const {
    if (name.startsWith(QLatin1Char('$'))) {
        QStringList parts = name.split(QRegExp(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const QString dyn = parts.takeFirst();
        const bool ok = m_dynamic ? m_dynamic(dyn, parts, out) : builtinDynamic(dyn, parts, out);
        if (!ok) appendUnique(&acc->undefined, name);
        return ok;
    }
    if (m_file.contains(name)) {
        if (stack->contains(name) || depth >= kMaxDepth) {
            appendUnique(&acc->cycles, name);
            return false;
        }
        stack->append(name);
        *out = substitute(m_file.value(name), depth + 1, stack, acc);
        stack->removeLast();
        return true;
    }
    if (m_env.contains(name)) {
        *out = m_env.value(name);
        return true;
    }
    appendUnique(&acc->undefined, name);
    return false;
}

QString Resolver::substitute(const QString &input, int depth, QStringList *stack,
                             ResolveResult *acc) const {
    QString out;
    int pos = 0;
    scanRefs(input, [&](int open, int end, const QString &name) {
        out += input.midRef(pos, open - pos);
        QString value;
        if (lookup(name, depth, stack, acc, &value)) out += value;
        else out += input.midRef(open, end - open);
        pos = end;
    });
    out += input.midRef(pos);
    return out;
}

ResolveResult Resolver::resolve(const QString &input) const {
    ResolveResult r;
    QStringList stack;
    r.text = substitute(input, 0, &stack, &r);
    return r;
}

QStringList referencedVariables(const QString &text) {
    QStringList out;
    scanRefs(text, [&](int, int, const QString &name) { appendUnique(&out, name); });
    return out;
}

bool hasVariables(const QString &text) { return !referencedVariables(text).isEmpty(); }

// ── Environments ────────────────────────────────────────────────────────

QMap<QString, QString> Environments::variablesFor(const QString &name) const {
    if (name.isEmpty() || !values.contains(name)) return {};
    QMap<QString, QString> out = shared;
    const QMap<QString, QString> &own = values[name];
    for (auto it = own.begin(); it != own.end(); ++it) out.insert(it.key(), it.value());
    return out;
}

Environments parseEnvironments(const QByteArray &publicJson, const QByteArray &privateJson) {
    Environments env;
    const struct { const QByteArray *data; const char *file; } docs[] = {
        {&publicJson, "http-client.env.json"},
        {&privateJson, "http-client.private.env.json"}};
    for (const auto &d : docs) {
        if (d.data->trimmed().isEmpty()) continue;
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(*d.data, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            const QString why = err.error != QJsonParseError::NoError
                                    ? err.errorString() + QStringLiteral(" at offset ") +
                                          QString::number(err.offset)
                                    : QStringLiteral("top level is not an object");
            if (!env.error.isEmpty()) env.error += QStringLiteral("; ");
            env.error += QString::fromLatin1(d.file) + QStringLiteral(": ") + why;
            continue;
        }
        const QJsonObject root = doc.object();
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (!it.value().isObject()) continue;
            const bool isShared = it.key() == QLatin1String("$shared");
            QMap<QString, QString> &target = isShared ? env.shared : env.values[it.key()];
            const QJsonObject vars = it.value().toObject();
            for (auto v = vars.begin(); v != vars.end(); ++v) {
                bool ok = false;
                const QString s = jsonScalar(v.value(), &ok);
                if (ok) target.insert(v.key(), s);
            }
        }
    }
    env.names = env.values.keys();  // QMap keys are sorted
    return env;
}

Environments loadEnvironments(const QString &dir) {
    auto readSmall = [&](const char *name) {
        QFile f(QDir(dir).filePath(QString::fromLatin1(name)));
        if (!f.exists() || f.size() > kMaxEnvFileBytes || !f.open(QIODevice::ReadOnly))
            return QByteArray();
        return f.readAll();
    };
    if (dir.isEmpty()) return {};
    return parseEnvironments(readSmall("http-client.env.json"),
                             readSmall("http-client.private.env.json"));
}

}  // namespace HttpFile
