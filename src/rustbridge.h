#ifndef RUSTBRIDGE_H
#define RUSTBRIDGE_H

#include <QString>
#include <QByteArray>
#include <QVector>
#include <notepad_core.h>

/**
 * C++ wrapper around the Rust core library.
 * Handles memory management automatically via RAII.
 */
namespace RustCore {

struct LoadResult {
    QString text;
    QString encoding;
    int eolMode;       // 0=LF, 1=CRLF, 2=CR
    quint64 fileSize;
    int status;        // 0=ok, 1=binary, 2=too_large, 3=error
    QString errorMsg;
    bool truncated;
};

LoadResult loadFile(const QString &path);
bool saveFile(const QString &path, const QString &text, const QString &encoding);

// Text operations
QString sortLines(const QString &text, int mode);
QString removeDuplicates(const QString &text, int mode);
QString removeEmptyLines(const QString &text, int mode);
QString trimLines(const QString &text, int mode);
QString reverseLines(const QString &text);
QString joinLines(const QString &text, const QString &separator);
QString convertCase(const QString &text, int mode);
QString convertWhitespace(const QString &text, int tabWidth, int mode);

// Search
QVector<size_t> findAll(const QString &text, const QString &pattern,
                        bool isRegex, bool caseSensitive, bool wholeWord);
size_t countMatches(const QString &text, const QString &pattern,
                    bool isRegex, bool caseSensitive);
QString replaceAll(const QString &text, const QString &pattern,
                   const QString &replacement, bool isRegex, bool caseSensitive);

// Hashing
QString computeHash(const QByteArray &data, int algo);
QString base64Encode(const QByteArray &data);
QString base64Decode(const QByteArray &data);
QString urlEncode(const QString &text);
QString urlDecode(const QString &text);

// Diff / Compare
struct DiffEntry {
    int tag;          // 0=equal, 1=insert, 2=delete
    int leftLine;
    int rightLine;
    QString text;
};

struct DiffInfo {
    QVector<DiffEntry> entries;
    int added, removed, changed;
};

DiffInfo computeDiff(const QString &left, const QString &right);

// SQL Formatter
// dialect: "ansi" | "postgres" | "mysql" | "mssql" | "sqlite" | "plsql"
// (empty == ansi). Also accepts combo-box labels like "T-SQL (SQL Server)".
QString formatSql(const QString &text, int indentWidth = 4, bool uppercase = true,
                  const QString &dialect = QStringLiteral("ansi"));

// JSON
QString formatJson(const QString &text, int indent = 4);
QString minifyJson(const QString &text);
QString fixJson(const QString &text);
QString fixJsonReport(const QString &text);

// HTML
QString formatHtml(const QString &text, int indent = 2);

// Brackets
QString fixBrackets(const QString &text);
QString checkBrackets(const QString &text);

} // namespace RustCore

#endif // RUSTBRIDGE_H
