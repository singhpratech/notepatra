#include "rustbridge.h"

namespace RustCore {

static QString fromRust(char *s) {
    if (!s) return QString();
    QString result = QString::fromUtf8(s);
    npc_free_string(s);
    return result;
}

static QString textFromResult(TextResult r) {
    if (!r.text) return QString();
    QString result = QString::fromUtf8(r.text, r.text_len);
    npc_free_text_result(r);
    return result;
}

static QByteArray toUtf8(const QString &s) {
    return s.toUtf8();
}

// ── File I/O ──

LoadResult loadFile(const QString &path) {
    QByteArray pathBytes = path.toUtf8();
    FileLoadResult r = npc_load_file(pathBytes.constData());

    LoadResult result;
    result.text = r.text ? QString::fromUtf8(r.text, r.text_len) : QString();
    result.encoding = fromRust(r.encoding);
    result.eolMode = r.eol_mode;
    result.fileSize = r.file_size;
    result.status = r.status;
    result.errorMsg = fromRust(r.error_msg);
    result.truncated = r.truncated != 0;

    npc_free_string(r.text);
    return result;
}

bool saveFile(const QString &path, const QString &text, const QString &encoding) {
    QByteArray pathBytes = path.toUtf8();
    QByteArray textBytes = text.toUtf8();
    QByteArray encBytes = encoding.toUtf8();
    return npc_save_file(pathBytes.constData(), textBytes.constData(),
                         textBytes.size(), encBytes.constData()) == 0;
}

// ── Text operations ──

QString sortLines(const QString &text, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_sort_lines(b.constData(), b.size(), mode));
}

QString removeDuplicates(const QString &text, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_remove_duplicates(b.constData(), b.size(), mode));
}

QString removeEmptyLines(const QString &text, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_remove_empty_lines(b.constData(), b.size(), mode));
}

QString trimLines(const QString &text, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_trim_lines(b.constData(), b.size(), mode));
}

QString reverseLines(const QString &text) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_reverse_lines(b.constData(), b.size()));
}

QString joinLines(const QString &text, const QString &separator) {
    QByteArray b = toUtf8(text);
    QByteArray s = toUtf8(separator);
    return textFromResult(npc_join_lines(b.constData(), b.size(), s.constData()));
}

QString convertCase(const QString &text, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_convert_case(b.constData(), b.size(), mode));
}

QString convertWhitespace(const QString &text, int tabWidth, int mode) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_convert_whitespace(b.constData(), b.size(), tabWidth, mode));
}

// ── Search ──

QVector<size_t> findAll(const QString &text, const QString &pattern,
                        bool isRegex, bool caseSensitive, bool wholeWord) {
    QByteArray t = toUtf8(text);
    QByteArray p = toUtf8(pattern);
    SearchResult r = npc_find_all(t.constData(), t.size(), p.constData(),
                                  isRegex ? 1 : 0, caseSensitive ? 1 : 0,
                                  wholeWord ? 1 : 0);
    QVector<size_t> positions;
    if (r.positions && r.count > 0) {
        positions.reserve(r.count);
        for (size_t i = 0; i < r.count; i++) {
            positions.append(r.positions[i]);
        }
        npc_free_matches(r);
    }
    return positions;
}

size_t countMatches(const QString &text, const QString &pattern,
                    bool isRegex, bool caseSensitive) {
    QByteArray t = toUtf8(text);
    QByteArray p = toUtf8(pattern);
    return npc_count_matches(t.constData(), t.size(), p.constData(),
                             isRegex ? 1 : 0, caseSensitive ? 1 : 0);
}

QString replaceAll(const QString &text, const QString &pattern,
                   const QString &replacement, bool isRegex, bool caseSensitive) {
    QByteArray t = toUtf8(text);
    QByteArray p = toUtf8(pattern);
    QByteArray r = toUtf8(replacement);
    return textFromResult(npc_replace_all(t.constData(), t.size(), p.constData(),
                                          r.constData(), isRegex ? 1 : 0,
                                          caseSensitive ? 1 : 0));
}

// ── Hashing ──

QString computeHash(const QByteArray &data, int algo) {
    HashResult r = npc_hash(data.constData(), data.size(), algo);
    return fromRust(r.hex);
}

QString base64Encode(const QByteArray &data) {
    return textFromResult(npc_base64_encode(data.constData(), data.size()));
}

QString base64Decode(const QByteArray &data) {
    return textFromResult(npc_base64_decode(data.constData(), data.size()));
}

QString urlEncode(const QString &text) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_url_encode(b.constData(), b.size()));
}

QString urlDecode(const QString &text) {
    QByteArray b = toUtf8(text);
    return textFromResult(npc_url_decode(b.constData(), b.size()));
}

// ── Diff ──

DiffInfo computeDiff(const QString &left, const QString &right) {
    QByteArray l = left.toUtf8();
    QByteArray r = right.toUtf8();
    ::DiffResult dr = npc_diff(l.constData(), l.size(), r.constData(), r.size());

    DiffInfo info;
    info.added = dr.added;
    info.removed = dr.removed;
    info.changed = dr.changed;

    if (dr.lines && dr.count > 0) {
        info.entries.reserve(dr.count);
        for (size_t i = 0; i < dr.count; i++) {
            DiffEntry entry;
            entry.tag = dr.lines[i].tag;
            entry.leftLine = dr.lines[i].left_line;
            entry.rightLine = dr.lines[i].right_line;
            entry.text = dr.lines[i].text ? QString::fromUtf8(dr.lines[i].text) : QString();
            info.entries.append(entry);
        }
        npc_free_diff(dr);
    }
    return info;
}

// ── SQL Formatter ──

QString formatSql(const QString &text, int indentWidth, bool uppercase) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_format_sql(b.constData(), b.size(), indentWidth, uppercase ? 1 : 0));
}

// ── JSON ──
QString formatJson(const QString &text, int indent) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_format_json(b.constData(), b.size(), indent));
}
QString minifyJson(const QString &text) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_minify_json(b.constData(), b.size()));
}
QString fixJson(const QString &text) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_fix_json(b.constData(), b.size()));
}
QString fixJsonReport(const QString &text) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_fix_json_report(b.constData(), b.size()));
}

// ── HTML ──
QString formatHtml(const QString &text, int indent) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_format_html(b.constData(), b.size(), indent));
}

// ── Brackets ──
QString fixBrackets(const QString &text) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_fix_brackets(b.constData(), b.size()));
}
QString checkBrackets(const QString &text) {
    QByteArray b = text.toUtf8();
    return textFromResult(npc_check_brackets(b.constData(), b.size()));
}

} // namespace RustCore
