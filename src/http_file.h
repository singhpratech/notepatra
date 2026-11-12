// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_HTTP_FILE_H
#define NOTEPATRA_HTTP_FILE_H

// Pure model of the ".http file" format used by the VS Code REST Client and
// the JetBrains HTTP Client: parsing, {{variable}} substitution and
// http-client.env.json environments. No widgets, so it is unit-testable.
//
// Format rules (see test_http_file.cpp for the table):
//   * A line starting with "###" AT COLUMN 0 separates requests; the text
//     after it is the request name. An indented "###" (e.g. inside a JSON
//     string in a body) is body text, not a separator.
//   * "#" and "//" lines are comments (outside a body). "# @name x" names
//     the request when the "###" line carries no name.
//   * "@name = value" before a request line defines a file-level variable.
//   * Request line: "METHOD URL [HTTP/x]"; a bare URL means GET. Following
//     lines starting with "?" or "&" continue the URL (VS Code format).
//   * Header lines until the first blank line. A line that is not a valid
//     "Token: value" header starts the body early (missing blank line).
//   * Body until the next separator or EOF; trailing blank lines dropped.
//   * CRLF and LF both work; a leading UTF-8 BOM is ignored.
//   * "< ./file" bodies are recognised but never read (bodyFromFile).

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

#include <functional>

namespace HttpFile {

using HeaderList = QList<QPair<QString, QString>>;

struct Request {
    QString name;
    QString method;       // upper-case, "GET" for a bare URL
    QString url;          // continuation lines already joined, unexpanded
    HeaderList headers;   // unexpanded
    QString body;         // unexpanded, lines joined with '\n'
    QString bodyFromFile; // path after "< " when the body is a file reference
    int startLine = 0;    // 0-based: separator line, or the block's first line
    int requestLine = 0;  // 0-based line of "METHOD URL"
    int endLine = 0;      // 0-based last line of the block (inclusive)
};

struct Document {
    QList<QPair<QString, QString>> variables;  // "@name = value", in file order
    QList<Request> requests;

    // Index of the request whose block contains `line`, or -1.
    int requestAtLine(int line) const;
};

Document parse(const QString &text);

// Headers as "Name: value" lines, and back.
QString headersToText(const HeaderList &headers);
HeaderList headersFromText(const QString &text);

// ── Variables ───────────────────────────────────────────────────────────
// Precedence, lowest to highest: environment, then file variables. File
// variable values may reference other variables (resolved recursively with
// a depth limit, cycles reported). Dynamic variables ($timestamp,
// $isoTimestamp, $uuid / $guid, $randomInt [min max]) are generated per
// reference. Substitution into a value is single-pass: text that a value
// inserts is never re-scanned.
struct ResolveResult {
    QString text;
    QStringList undefined;  // names with no definition (deduplicated)
    QStringList cycles;     // names whose definition refers back to itself
    bool ok() const { return undefined.isEmpty() && cycles.isEmpty(); }
};

class Resolver {
public:
    Resolver() = default;
    Resolver(const QMap<QString, QString> &environment,
             const QList<QPair<QString, QString>> &fileVariables);

    ResolveResult resolve(const QString &input) const;

    // Test hook: replaces the generator for $-prefixed names. Return false
    // for an unknown name.
    using DynamicFn = std::function<bool(const QString &name, const QStringList &args,
                                         QString *out)>;
    void setDynamicGenerator(DynamicFn fn) { m_dynamic = std::move(fn); }

    static bool builtinDynamic(const QString &name, const QStringList &args, QString *out);

private:
    bool lookup(const QString &name, int depth, QStringList *stack, ResolveResult *acc,
                QString *out) const;
    QString substitute(const QString &input, int depth, QStringList *stack,
                       ResolveResult *acc) const;

    QMap<QString, QString> m_env;
    QMap<QString, QString> m_file;
    DynamicFn m_dynamic;
};

// Names referenced as {{name}} in `text` (dynamic ones included, "$" kept).
QStringList referencedVariables(const QString &text);
bool hasVariables(const QString &text);

// ── Environments (http-client.env.json + http-client.private.env.json) ──
struct Environments {
    QStringList names;                         // sorted, "$shared" excluded
    QMap<QString, QMap<QString, QString>> values;  // private merged over public
    QMap<QString, QString> shared;             // "$shared", applied under every env
    QString error;                             // parse error, if any

    QMap<QString, QString> variablesFor(const QString &name) const;
};

// Parses one or both JSON documents (empty QByteArray = file absent).
Environments parseEnvironments(const QByteArray &publicJson, const QByteArray &privateJson);
// Reads http-client.env.json and http-client.private.env.json from `dir`.
Environments loadEnvironments(const QString &dir);

}  // namespace HttpFile

#endif  // NOTEPATRA_HTTP_FILE_H
