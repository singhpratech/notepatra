// SPDX-License-Identifier: GPL-3.0-or-later

#include "csvanalyst.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QUuid>
#include <QVariant>

#include <limits>

namespace CsvAnalyst {

QString columnTypeName(ColumnType t) {
    switch (t) {
        case ColumnType::Integer:  return QStringLiteral("INTEGER");
        case ColumnType::Real:     return QStringLiteral("REAL");
        case ColumnType::Boolean:  return QStringLiteral("BOOLEAN");
        case ColumnType::Date:     return QStringLiteral("DATE");
        case ColumnType::DateTime: return QStringLiteral("DATETIME");
        case ColumnType::Text:     return QStringLiteral("TEXT");
    }
    return QStringLiteral("TEXT");
}

QString sqliteTypeFor(ColumnType t) {
    // SQLite uses type affinity, not strict types. Match value layout —
    // it preserves the underlying value while letting the model write
    // numeric WHEREs that compare correctly.
    switch (t) {
        case ColumnType::Integer:  return QStringLiteral("INTEGER");
        case ColumnType::Real:     return QStringLiteral("REAL");
        case ColumnType::Boolean:  return QStringLiteral("INTEGER");
        case ColumnType::Date:
        case ColumnType::DateTime: return QStringLiteral("TEXT");
        case ColumnType::Text:     return QStringLiteral("TEXT");
    }
    return QStringLiteral("TEXT");
}

bool looksLikeCsv(const QString &pathOrName) {
    QString lower = pathOrName.toLower();
    return lower.endsWith(".csv") || lower.endsWith(".tsv") ||
           lower.endsWith(".psv") || lower.endsWith(".tab");
}

// ── Record reader (RFC 4180) ───────────────────────────────────────────
// A field that opens with '"' may hold the delimiter, doubled quotes and line
// breaks, so one record can span several physical lines. A '"' inside an
// unquoted field is literal (5" screen). Single pass over each line: linear.
struct LineReader {
    QTextStream *ts = nullptr;
    const QStringList *lines = nullptr;
    int pos = 0;
    bool next(QString *out) {
        if (ts) {
            if (ts->atEnd()) return false;
            *out = ts->readLine();
            return true;
        }
        if (pos >= lines->size()) return false;
        *out = lines->at(pos++);
        return true;
    }
};

// Reads one record. Returns false at end of input; a blank line yields an
// empty field list. `raw` (optional) receives the record's source text.
static bool parseRecord(LineReader &in, QChar delim, QStringList *fields, QString *raw = nullptr) {
    QString line;
    if (!in.next(&line)) return false;
    if (line.startsWith(QChar(0xFEFF))) line.remove(0, 1);  // stray UTF-8 BOM
    fields->clear();
    if (raw) *raw = line;
    if (line.isEmpty()) return true;
    QString cur;
    bool inQuote = false;   // inside a quoted field
    bool quoted = false;    // current field opened with a quote
    bool blank = true;      // current field holds only whitespace so far
    int i = 0;
    for (;;) {
        if (i >= line.size()) {
            if (inQuote && in.next(&line)) {    // quoted line break: record continues
                cur += QLatin1Char('\n');
                if (raw) { raw->append(QLatin1Char('\n')); raw->append(line); }
                i = 0;
                continue;
            }
            break;
        }
        const QChar c = line[i++];
        if (inQuote) {
            if (c == QLatin1Char('"')) {
                if (i < line.size() && line[i] == QLatin1Char('"')) { cur += c; ++i; }
                else inQuote = false;
            } else {
                cur += c;
            }
        } else if (c == delim) {
            fields->append(cur);
            cur.clear();
            quoted = false;
            blank = true;
        } else if (c == QLatin1Char('"') && !quoted && blank) {
            cur.clear();                        // drop spaces before the opening quote
            inQuote = quoted = true;
        } else {
            cur += c;
            if (!c.isSpace()) blank = false;
        }
    }
    fields->append(cur);
    return true;
}

// ── Delimiter detection ────────────────────────────────────────────────
// Sniff among , \t ; | by parsing the sample as records under each candidate
// (so quoted delimiters do not count) and picking the most consistent one.
static QChar sniffDelimiter(const QStringList &sampleLines, QString *warning) {
    static const QList<QChar> kCandidates = {',', '\t', ';', '|'};
    QChar best = ',';
    int bestScore = -1;

    for (const QChar &cand : kCandidates) {
        if (sampleLines.isEmpty()) continue;
        QHash<int,int> counts;
        LineReader in;
        in.lines = &sampleLines;
        QStringList fields;
        while (parseRecord(in, cand, &fields)) {
            if (fields.isEmpty()) continue;
            counts[fields.size() - 1]++;
        }
        int peak = 0;
        int columnCount = 0;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            if (it.value() > peak) { peak = it.value(); columnCount = it.key(); }
        }
        // Only consider the candidate "real" if it produces ≥1 column.
        if (columnCount == 0) continue;
        // Score = (peak frequency) * (columnCount + 1). Bias toward more
        // columns at high consistency — avoids picking ',' on a TSV that
        // has stray commas.
        int score = peak * (columnCount + 1);
        if (score > bestScore) { bestScore = score; best = cand; }
    }
    if (bestScore < 0 && warning) {
        *warning = QStringLiteral("Could not confidently detect a delimiter; defaulted to ','.");
    }
    return best;
}

// ── Value parsing ──────────────────────────────────────────────────────
// Numbers: plain, plus "$1,234.50", "1,234", "(1,234.50)" (accounting
// negative) and "12.5%" (stored as 12.5). Grouping must be exact (1,234 not
// 12,34) and the decimal mark is '.', so "1.234,50" stays text (no locale guess).
struct NumParse {
    QString clean;          // canonical "-1234.50" form
    bool integral = false;  // no fractional part written
    bool decorated = false; // had $, grouping, parentheses or %
    bool percent = false;
    QChar currency;
};

static bool isCurrencySymbol(QChar c) {
    const ushort u = c.unicode();
    return u == '$' || u == 0x20AC || u == 0x00A3 || u == 0x00A5 || u == 0x20B9;
}

static bool parseNumber(const QString &raw, NumParse *out) {
    const QString s = raw.trimmed();
    if (s.isEmpty()) return false;
    *out = NumParse();
    bool ok = false;
    s.toLongLong(&ok);
    if (ok) { out->clean = s; out->integral = true; return true; }
    s.toDouble(&ok);
    if (ok) { out->clean = s; return true; }

    int b = 0, e = s.size();
    bool neg = false, paren = false, sign = false;
    if (s[b] == QLatin1Char('(') && s[e - 1] == QLatin1Char(')')) {
        paren = neg = true;
        ++b; --e;
    }
    auto takeSign = [&]() {
        if (b < e && (s[b] == QLatin1Char('-') || s[b] == QLatin1Char('+')) && !sign && !paren) {
            neg = (s[b] == QLatin1Char('-'));
            sign = true;
            ++b;
        }
    };
    takeSign();
    if (b < e && isCurrencySymbol(s[b])) { out->currency = s[b]; ++b; }
    takeSign();
    if (b < e && s[e - 1] == QLatin1Char('%')) { out->percent = true; --e; }

    QString digits;
    int intDigits = 0, groupLen = -1, nDigits = 0;  // groupLen: digits since last ','
    bool dot = false;
    for (int i = b; i < e; ++i) {
        const ushort c = s[i].unicode();
        if (c >= '0' && c <= '9') {
            if (!dot) {
                if (groupLen >= 0) { if (++groupLen > 3) return false; }
                else ++intDigits;
            }
            digits += QChar(c);
            ++nDigits;
        } else if (c == ',') {
            if (dot || (groupLen < 0 ? (intDigits < 1 || intDigits > 3 || digits[0] == QLatin1Char('0'))
                                     : groupLen != 3))
                return false;
            groupLen = 0;
        } else if (c == '.') {
            if (dot || (groupLen >= 0 && groupLen != 3)) return false;
            dot = true;
            digits += QLatin1Char('.');
        } else {
            return false;
        }
    }
    if (nDigits == 0 || (!dot && groupLen >= 0 && groupLen != 3)) return false;
    out->decorated = paren || !out->currency.isNull() || out->percent || groupLen >= 0;
    if (!out->decorated) return false;  // plain forms were handled above
    out->clean = (neg ? QStringLiteral("-") : QString()) + digits;
    out->integral = !dot;
    return true;
}

static bool isBoolLike(const QString &s) {
    if (s.isEmpty()) return true;
    QString l = s.toLower();
    return l == "true" || l == "false" || l == "yes" || l == "no" ||
           l == "t" || l == "f" || l == "0" || l == "1";
}

// Date layouts a column can be stored from. Stored form is ISO yyyy-MM-dd.
enum DateLayout { NoDate = 0, IsoDate = 1, YmdSlash = 2, Mdy = 3, Dmy = 4 };

// Splits "a<sep>b<sep>c" of 1-4 ASCII digits each; lens receive digit counts.
static bool dateParts(const QString &s, QChar sep, int v[3], int len[3]) {
    int part = 0;
    v[0] = v[1] = v[2] = 0;
    len[0] = len[1] = len[2] = 0;
    for (const QChar ch : s) {
        const ushort c = ch.unicode();
        if (c >= '0' && c <= '9') {
            if (++len[part] > 4) return false;
            v[part] = v[part] * 10 + (c - '0');
        } else if (ch == sep && part < 2 && len[part] > 0) {
            ++part;
        } else {
            return false;
        }
    }
    return part == 2 && len[2] > 0;
}

static int fullYear(int y, int digits) {
    if (digits == 4) return y;
    if (digits == 2) return y < 69 ? 2000 + y : 1900 + y;  // POSIX %y pivot
    return -1;
}

static QDate parseDate(const QString &raw, int layout) {
    const QString s = raw.trimmed();
    int v[3], len[3];
    switch (layout) {
        case IsoDate:
        case YmdSlash:
            if (!dateParts(s, layout == IsoDate ? QLatin1Char('-') : QLatin1Char('/'), v, len)
                || len[0] != 4 || len[1] > 2 || len[2] > 2)
                return QDate();
            return QDate(v[0], v[1], v[2]);
        case Mdy:
        case Dmy: {
            if (!dateParts(s, QLatin1Char('/'), v, len) || len[0] > 2 || len[1] > 2)
                return QDate();
            const int y = fullYear(v[2], len[2]);
            if (y < 0) return QDate();
            return layout == Mdy ? QDate(y, v[0], v[1]) : QDate(y, v[1], v[0]);
        }
        default:
            return QDate();
    }
}

// ISO date-time ("2026-04-15T10:30:00", "2026-04-15 10:30:00.123"); parsed
// without a time zone so a DST gap cannot invalidate it.
static bool isIsoDateTime(const QString &raw) {
    const QString s = raw.trimmed();
    if (s.size() < 19 || s.size() > 23 || (s[10] != QLatin1Char('T') && s[10] != QLatin1Char(' ')))
        return false;
    const QString d = s.left(10);
    if (d[4] != QLatin1Char('-') || d[7] != QLatin1Char('-') || !parseDate(d, IsoDate).isValid())
        return false;
    const QString t = s.mid(11);
    return QTime::fromString(t, QStringLiteral("HH:mm:ss")).isValid()
        || QTime::fromString(t, QStringLiteral("HH:mm:ss.zzz")).isValid();
}

static bool isNullToken(const QString &v) {
    static const QSet<QString> k = {"NA", "N/A", "#N/A", "NULL", "NONE", "NAN", "-", "--", "?"};
    return v.isEmpty() || k.contains(v.toUpper());
}

// ── Type inference ────────────────────────────────────────────────────
// A column takes a type only when every non-empty value fits it. Order:
// INTEGER, BOOLEAN, REAL, decorated numbers, then dates. A slash date column
// whose values read as both M/D/Y and D/M/Y (no day > 12) stays TEXT.
static void inferColumn(const QStringList &values, ColumnInfo *ci) {
    ci->type = ColumnType::Text;
    ci->percent = false;
    ci->dateLayout = NoDate;
    bool any = false, allInt = true, allReal = true, sawNonInt = false, allBool = true;
    bool allNum = true, anyDecorated = false, allIntegral = true;
    int pct = 0, nonEmpty = 0;
    QSet<QChar> currencies;
    bool iso = true, isoDt = false, ymd = true, mdy = true, dmy = true;
    for (const QString &v : values) {
        if (v.isEmpty()) continue;
        any = true;
        ++nonEmpty;
        bool okI = false, okR = false;
        v.toLongLong(&okI);
        if (!okI) v.toDouble(&okR);
        if (!okI) allInt = false;
        if (!okI && !okR) allReal = false;
        if (!okI && okR) sawNonInt = true;
        if (allBool && !isBoolLike(v)) allBool = false;
        if (allNum) {
            NumParse np;
            if (!parseNumber(v, &np)) {
                allNum = false;
            } else {
                anyDecorated = anyDecorated || np.decorated;
                allIntegral = allIntegral && np.integral;
                if (np.percent) ++pct;
                if (!np.currency.isNull()) currencies.insert(np.currency);
            }
        }
        if (iso) {
            if (isIsoDateTime(v)) isoDt = true;
            else if (!parseDate(v, IsoDate).isValid()) iso = false;
        }
        if (ymd && !parseDate(v, YmdSlash).isValid()) ymd = false;
        if (mdy && !parseDate(v, Mdy).isValid()) mdy = false;
        if (dmy && !parseDate(v, Dmy).isValid()) dmy = false;
    }
    if (!any) return;
    if (allInt) { ci->type = ColumnType::Integer; return; }
    if (allBool) { ci->type = ColumnType::Boolean; return; }
    if (allReal && sawNonInt) { ci->type = ColumnType::Real; return; }
    if (allNum && anyDecorated && currencies.size() <= 1 && (pct == 0 || pct == nonEmpty)) {
        ci->type = allIntegral ? ColumnType::Integer : ColumnType::Real;
        ci->percent = pct > 0;
        return;
    }
    if (iso) {
        ci->type = isoDt ? ColumnType::DateTime : ColumnType::Date;
        ci->dateLayout = IsoDate;
    } else if (ymd) {
        ci->type = ColumnType::Date;
        ci->dateLayout = YmdSlash;
    } else if (mdy != dmy) {
        ci->type = ColumnType::Date;
        ci->dateLayout = mdy ? Mdy : Dmy;
    }
}

// The value bound into SQLite for column `ci` (null for an empty field).
static QVariant storedValue(const ColumnInfo &ci, const QString &v) {
    if (v.isEmpty()) return QVariant();
    switch (ci.type) {
        case ColumnType::Integer: {
            bool ok = false;
            const qint64 i = v.toLongLong(&ok);
            if (ok) return QVariant(i);
            NumParse np;
            if (parseNumber(v, &np)) {
                const qint64 j = np.clean.toLongLong(&ok);
                if (ok) return QVariant(j);
                const double d = np.clean.toDouble(&ok);
                if (ok) return QVariant(d);
            }
            return QVariant(v);
        }
        case ColumnType::Real: {
            bool ok = false;
            const double d = v.toDouble(&ok);
            if (ok) return QVariant(d);
            NumParse np;
            if (parseNumber(v, &np)) {
                const double e = np.clean.toDouble(&ok);
                if (ok) return QVariant(e);
            }
            return QVariant(v);
        }
        case ColumnType::Boolean: {
            bool ok = false;
            const double d = v.toDouble(&ok);
            return ok ? QVariant(d) : QVariant(v);
        }
        case ColumnType::Date: {
            const QDate d = parseDate(v, ci.dateLayout);
            return d.isValid() ? QVariant(d.toString(QStringLiteral("yyyy-MM-dd"))) : QVariant(v);
        }
        case ColumnType::DateTime:
        case ColumnType::Text:
            break;
    }
    return QVariant(v);
}

// ── Header detection ───────────────────────────────────────────────────
// Original heuristic: if the first row contains no numeric-like fields AND
// every field is non-empty, it's a header.
static bool firstRowLooksLikeHeader(const QStringList &row) {
    if (row.isEmpty()) return false;
    int alphaFields = 0;
    for (const QString &f : row) {
        if (f.isEmpty()) return false;
        bool ok = false;
        f.toDouble(&ok);
        if (!ok) ++alphaFields;
    }
    return alphaFields == row.size();
}

// Does a first-row cell fit the type inferred from the rows below it?
static bool fitsColumn(const QString &v, const ColumnInfo &ci) {
    NumParse np;
    switch (ci.type) {
        case ColumnType::Integer:
        case ColumnType::Real:     return parseNumber(v, &np);
        case ColumnType::Boolean:  return isBoolLike(v);
        case ColumnType::Date:     return parseDate(v, ci.dateLayout).isValid() || isIsoDateTime(v);
        case ColumnType::DateTime: return parseDate(v, IsoDate).isValid() || isIsoDateTime(v);
        case ColumnType::Text:     return true;
    }
    return true;
}

// Sanitize a column name to a safe SQL identifier — letters, digits,
// underscore. SQLite quoting via "" is fine but the model is happier with
// simple names.
static QString sanitizeIdent(const QString &raw) {
    QString out;
    for (QChar c : raw) {
        if (c.isLetterOrNumber() || c == '_') out += c;
        else out += '_';
    }
    if (out.isEmpty()) return QStringLiteral("col");
    if (out[0].isDigit()) out = QStringLiteral("col_") + out;
    return out;
}

static QVector<ColumnInfo> inferColumns(const QVector<QStringList> &rows, int from, int ncols) {
    QVector<ColumnInfo> cols(ncols);
    QStringList vals;
    for (int c = 0; c < ncols; ++c) {
        vals.clear();
        for (int r = from; r < rows.size(); ++r) vals.append(rows[r].value(c));
        inferColumn(vals, &cols[c]);
    }
    return cols;
}

// ── detectSchema ────────────────────────────────────────────────────────

CsvSchema detectSchema(const QString &filePath, int maxScanRows) {
    CsvSchema schema;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return schema;
    schema.fileSize = f.size();

    QStringList sampleLines;
    {
        QTextStream ts(&f);
        while (!ts.atEnd() && sampleLines.size() < maxScanRows)
            sampleLines.append(ts.readLine());
    }
    if (sampleLines.isEmpty()) return schema;

    QString warn;
    schema.delimiter = sniffDelimiter(sampleLines, &warn);
    schema.detectionWarning = warn;

    // Re-read as records (a quoted field may span lines) up to maxScanRows.
    f.seek(0);
    QTextStream ts(&f);
    LineReader in;
    in.ts = &ts;
    QVector<QStringList> records;
    QStringList fields;
    while (records.size() < maxScanRows && parseRecord(in, schema.delimiter, &fields)) {
        if (!fields.isEmpty()) records.append(fields);
    }
    if (records.isEmpty()) return schema;
    const bool moreRows = !ts.atEnd();

    const QStringList firstRow = records.first();
    const int ncols = firstRow.size();
    for (QStringList &r : records) {
        // Pad / truncate to firstRow column count for stable column indexing.
        while (r.size() < ncols) r.append(QString());
        if (r.size() > ncols) r = r.mid(0, ncols);
    }

    // Header: a first-row cell that contradicts the typed data below it
    // ("amount" over numbers) marks a header even when another header cell
    // is numeric ("2024") or empty (a pandas index column).
    QVector<ColumnInfo> below = inferColumns(records, 1, ncols);
    int headerVotes = 0;
    for (int c = 0; c < ncols; ++c) {
        const QString v = firstRow[c].trimmed();
        if (isNullToken(v) || below[c].type == ColumnType::Text) continue;
        if (!fitsColumn(v, below[c])) ++headerVotes;
    }
    schema.hasHeader = (records.size() > 1 && headerVotes > 0) || firstRowLooksLikeHeader(firstRow);
    const int dataStart = schema.hasHeader ? 1 : 0;
    QVector<ColumnInfo> typed = schema.hasHeader ? below : inferColumns(records, 0, ncols);

    // Build columns
    schema.columns.reserve(ncols);
    QSet<QString> usedIdents;
    for (int c = 0; c < ncols; ++c) {
        ColumnInfo ci = typed[c];
        ci.name = schema.hasHeader && !firstRow[c].trimmed().isEmpty()
            ? firstRow[c].trimmed()
            : QStringLiteral("col_%1").arg(c + 1);
        // The queryable name: what the `csv` table and the preview both use.
        QString ident = sanitizeIdent(ci.name);
        const QString base = ident;
        int n = 2;
        while (usedIdents.contains(ident.toLower()))
            ident = QStringLiteral("%1_%2").arg(base).arg(n++);
        usedIdents.insert(ident.toLower());
        ci.ident = ident;

        const bool numeric = ci.type == ColumnType::Integer || ci.type == ColumnType::Real;
        const bool converts = numeric || ci.type == ColumnType::Date;
        double mn = std::numeric_limits<double>::infinity();
        double mx = -std::numeric_limits<double>::infinity();
        bool anyNum = false;
        QSet<QString> seen;
        for (int r = dataStart; r < records.size(); ++r) {
            const QString &v = records[r][c];
            if (v.isEmpty()) { ci.nullCount++; continue; }
            ci.nonNullCount++;
            const QVariant sv = converts ? storedValue(ci, v) : QVariant(v);
            const QString shown = sv.toString();
            if (converts && ci.sourceExample.isEmpty() && shown != v.trimmed())
                ci.sourceExample = v;
            if (ci.sampleValues.size() < 5 && !seen.contains(shown)) {
                ci.sampleValues.append(shown);
                seen.insert(shown);
            }
            if (numeric && (sv.type() == QVariant::LongLong || sv.type() == QVariant::Double)) {
                const double d = sv.toDouble();
                if (d < mn) mn = d;
                if (d > mx) mx = d;
                anyNum = true;
            }
        }
        if (anyNum) {
            ci.sampleMin = (ci.type == ColumnType::Integer)
                ? QString::number(static_cast<long long>(mn))
                : QString::number(mn, 'g', 6);
            ci.sampleMax = (ci.type == ColumnType::Integer)
                ? QString::number(static_cast<long long>(mx))
                : QString::number(mx, 'g', 6);
        }
        schema.columns.append(ci);
    }

    schema.rowCount = moreRows ? -1  // -1 means "more than maxScanRows; full count unknown"
                               : records.size() - dataStart;
    return schema;
}

// ── buildPreviewText ─────────────────────────────────────────────────────

QString buildPreviewText(const QString &filePath,
                         int headRows,
                         int tailRows,
                         int maxBytes) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    f.close();

    CsvSchema schema = detectSchema(filePath, 1000);
    QStringList lines;
    lines.append(QStringLiteral("CSV: %1").arg(QFileInfo(filePath).fileName()));
    lines.append(QStringLiteral("Size: %1 bytes · Delimiter: %2 · Header: %3 · Rows: %4")
        .arg(schema.fileSize)
        .arg(schema.delimiter == '\t' ? QStringLiteral("\\t") : QString(schema.delimiter))
        .arg(schema.hasHeader ? QStringLiteral("yes") : QStringLiteral("no"))
        .arg(schema.rowCount < 0 ? QStringLiteral(">1000") : QString::number(schema.rowCount)));
    if (!schema.detectionWarning.isEmpty())
        lines.append(QStringLiteral("Note: %1").arg(schema.detectionWarning));

    lines.append(QString());
    lines.append(QStringLiteral("Schema: table csv; use these exact column names in SQL"));
    for (const auto &c : schema.columns) {
        QString line = QStringLiteral("  %1 (%2)").arg(c.ident, columnTypeName(c.type));
        if (c.name != c.ident)
            line += QStringLiteral(" — header \"%1\"").arg(c.name);
        if (c.nullCount > 0)
            line += QStringLiteral(" — %1 nulls").arg(c.nullCount);
        if (!c.sampleMin.isEmpty())
            line += QStringLiteral(" · range %1 .. %2").arg(c.sampleMin, c.sampleMax);
        if (!c.sampleValues.isEmpty()) {
            QStringList sv = c.sampleValues;
            for (auto &v : sv) {
                v.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
                if (v.length() > 20) v = v.left(17) + QStringLiteral("...");
            }
            line += QStringLiteral(" · e.g. ") + sv.join(", ");
        }
        if (!c.sourceExample.isEmpty())
            line += QStringLiteral(" · stored converted from text like \"%1\"%2")
                .arg(c.sourceExample.left(20),
                     c.percent ? QStringLiteral(" (percent points)") : QString());
        lines.append(line);
    }

    // Head + tail (whole records, so a quoted multi-line field stays intact)
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        LineReader in;
        in.ts = &ts;
        QStringList allLines;
        QStringList fields;
        QString raw;
        while (parseRecord(in, schema.delimiter, &fields, &raw)) {
            if (fields.isEmpty()) continue;
            allLines.append(raw);
            if (allLines.size() > 100000) break;  // hard cap
        }
        f.close();

        if (!allLines.isEmpty()) {
            int dataStart = schema.hasHeader ? 1 : 0;
            int n = allLines.size();
            lines.append(QString());
            lines.append(QStringLiteral("First %1 row(s):").arg(qMin(headRows, n - dataStart)));
            for (int i = dataStart; i < qMin(dataStart + headRows, n); ++i) {
                lines.append(QStringLiteral("  ") + allLines[i]);
            }
            if (n - dataStart > headRows + tailRows) {
                int tailStart = qMax(dataStart + headRows, n - tailRows);
                lines.append(QStringLiteral("  ..."));
                lines.append(QStringLiteral("Last %1 row(s):").arg(n - tailStart));
                for (int i = tailStart; i < n; ++i) {
                    lines.append(QStringLiteral("  ") + allLines[i]);
                }
            }
        }
    }

    QString out = lines.join(QStringLiteral("\n"));
    if (out.toUtf8().size() > maxBytes) {
        // Truncate to byte budget.
        int cut = out.size();
        while (cut > 0 && out.left(cut).toUtf8().size() > maxBytes - 32) {
            cut -= 64;
        }
        out = out.left(cut).trimmed() + QStringLiteral("\n[...truncated]");
    }
    return out;
}

// ── loadIntoSqlite ──────────────────────────────────────────────────────

bool loadIntoSqlite(const QString &filePath,
                    int maxRows,
                    QString *outConnectionName,
                    bool *outTruncated,
                    QString *outError) {
    if (outTruncated) *outTruncated = false;

    if (!QSqlDatabase::drivers().contains("QSQLITE")) {
        if (outError) *outError = QStringLiteral(
            "QSQLITE driver is not available — cannot load CSV into in-memory database.");
        return false;
    }

    CsvSchema schema = detectSchema(filePath, 1000);
    if (schema.columns.isEmpty()) {
        if (outError) *outError = QStringLiteral(
            "Could not detect any columns in '%1'.").arg(filePath);
        return false;
    }

    const QString cname = QStringLiteral("notepatra-csv-%1")
        .arg(QUuid::createUuid().toString(QUuid::Id128));
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", cname);
    db.setDatabaseName(":memory:");
    if (!db.open()) {
        if (outError) *outError = db.lastError().text();
        QSqlDatabase::removeDatabase(cname);
        return false;
    }

    // Build CREATE TABLE (column names come from the schema: one source of truth)
    QStringList colDefs;
    QStringList placeholders;
    for (const auto &c : schema.columns) {
        colDefs.append(QStringLiteral("\"%1\" %2").arg(c.ident, sqliteTypeFor(c.type)));
        placeholders.append(QStringLiteral("?"));
    }
    const QString createSql = QStringLiteral(
        "CREATE TABLE csv (%1)").arg(colDefs.join(", "));

    {
        QSqlQuery q(db);
        if (!q.exec(createSql)) {
            if (outError) *outError = q.lastError().text();
            db.close();
            QSqlDatabase::removeDatabase(cname);
            return false;
        }
        q.exec("BEGIN");
    }

    // Stream rows into the table.
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (outError) *outError = QStringLiteral("Could not open '%1' for reading.").arg(filePath);
        db.close();
        QSqlDatabase::removeDatabase(cname);
        return false;
    }
    QTextStream ts(&f);
    LineReader in;
    in.ts = &ts;
    int rowsLoaded = 0;
    int rowsRead = 0;
    int badRows = 0;
    {
        QSqlQuery q(db);
        const QString insertSql = QStringLiteral("INSERT INTO csv VALUES (%1)")
            .arg(placeholders.join(", "));
        q.prepare(insertSql);
        QStringList parts;
        const int ncols = schema.columns.size();
        while (parseRecord(in, schema.delimiter, &parts)) {
            if (parts.isEmpty()) continue;                     // blank line
            if (rowsRead++ == 0 && schema.hasHeader) continue; // header record
            while (parts.size() < ncols) parts.append(QString());
            for (int c = 0; c < ncols; ++c)
                q.bindValue(c, storedValue(schema.columns[c], parts[c]));
            if (q.exec()) ++rowsLoaded;
            else ++badRows;
            if (rowsLoaded >= maxRows) {
                if (outTruncated) *outTruncated = true;
                break;
            }
        }
    }
    {
        QSqlQuery q(db);
        q.exec("COMMIT");
    }

    if (outConnectionName) *outConnectionName = cname;
    if (outError && badRows > 0) {
        *outError = QStringLiteral("Loaded %1 rows (skipped %2 unparseable).")
            .arg(rowsLoaded).arg(badRows);
    }
    return true;
}

} // namespace CsvAnalyst
