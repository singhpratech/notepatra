// SPDX-License-Identifier: GPL-3.0-or-later

#include "csvanalyst.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTextStream>
#include <QUuid>

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

// ── Delimiter detection ────────────────────────────────────────────────
// Sniff among , \t ; | by counting per-row consistency. Picks the
// candidate that yields the most consistent column count across the
// sample.
static QChar sniffDelimiter(const QStringList &sampleLines, QString *warning) {
    static const QList<QChar> kCandidates = {',', '\t', ';', '|'};
    QChar best = ',';
    int bestScore = -1;

    for (const QChar &cand : kCandidates) {
        if (sampleLines.isEmpty()) continue;
        QHash<int,int> counts;
        for (const QString &line : sampleLines) {
            counts[line.count(cand)]++;
        }
        int peak = 0, total = 0;
        int columnCount = 0;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            total += it.value();
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

// ── Single-line CSV split (RFC4180-ish — handles "quoted, fields") ─────
// Returns a list of fields. Caller should pass exactly one row.
static QStringList splitCsvLine(const QString &line, QChar delim) {
    QStringList out;
    QString cur;
    bool inQuote = false;
    for (int i = 0; i < line.size(); ++i) {
        QChar c = line[i];
        if (inQuote) {
            if (c == '"') {
                // Doubled "" is a literal quote inside quoted field.
                if (i + 1 < line.size() && line[i+1] == '"') {
                    cur += '"';
                    ++i;
                } else {
                    inQuote = false;
                }
            } else {
                cur += c;
            }
        } else {
            if (c == '"') {
                inQuote = true;
            } else if (c == delim) {
                out.append(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
    }
    out.append(cur);
    return out;
}

// ── Type inference ────────────────────────────────────────────────────
// Conservative: a column is Integer iff every non-empty value parses as
// an int; Real if it parses as a float (and at least one is non-int);
// Boolean if every value is in {true,false,0,1,yes,no,t,f}; Date /
// DateTime if values match common ISO/US formats.
static bool isIntegerLike(const QString &s) {
    if (s.isEmpty()) return true;  // null counts as compatible
    bool ok = false;
    s.toLongLong(&ok);
    return ok;
}

static bool isRealLike(const QString &s) {
    if (s.isEmpty()) return true;
    bool ok = false;
    s.toDouble(&ok);
    return ok;
}

static bool isBoolLike(const QString &s) {
    if (s.isEmpty()) return true;
    QString l = s.toLower();
    return l == "true" || l == "false" || l == "yes" || l == "no" ||
           l == "t" || l == "f" || l == "0" || l == "1";
}

static bool isDateLike(const QString &s, bool *isDateTime) {
    if (s.isEmpty()) return true;
    static const QStringList kDateFormats = {
        "yyyy-MM-dd", "yyyy/MM/dd", "MM/dd/yyyy", "dd/MM/yyyy",
        "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-dd HH:mm:ss",
        "yyyy-MM-ddTHH:mm:ss.zzz", "yyyy-MM-dd HH:mm:ss.zzz"
    };
    for (const QString &fmt : kDateFormats) {
        QDateTime dt = QDateTime::fromString(s, fmt);
        if (dt.isValid()) {
            if (isDateTime) *isDateTime = fmt.contains('H');
            return true;
        }
    }
    return false;
}

static ColumnType inferType(const QStringList &values) {
    if (values.isEmpty()) return ColumnType::Text;
    bool allInt = true, allReal = true, allBool = true, allDate = true, allDateTime = true;
    bool sawNonInt = false, sawDateTimeFmt = false, sawAnyNonEmpty = false;
    for (const QString &v : values) {
        if (v.isEmpty()) continue;
        sawAnyNonEmpty = true;
        if (allInt && !isIntegerLike(v)) { allInt = false; }
        if (!isIntegerLike(v) && allReal && isRealLike(v)) sawNonInt = true;
        if (allReal && !isRealLike(v)) allReal = false;
        if (allBool && !isBoolLike(v)) allBool = false;
        bool isDt = false;
        if (allDate && !isDateLike(v, &isDt)) allDate = false;
        if (isDt) sawDateTimeFmt = true;
    }
    if (!sawAnyNonEmpty) return ColumnType::Text;
    if (allInt) return ColumnType::Integer;
    if (allBool) return ColumnType::Boolean;
    if (allReal && sawNonInt) return ColumnType::Real;
    if (allDate) return sawDateTimeFmt ? ColumnType::DateTime : ColumnType::Date;
    return ColumnType::Text;
}

// ── Header detection ───────────────────────────────────────────────────
// Heuristic: if the first row contains no numeric-like fields AND every
// field is non-empty, it's a header. Otherwise it's data.
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

// ── detectSchema ────────────────────────────────────────────────────────

CsvSchema detectSchema(const QString &filePath, int maxScanRows) {
    CsvSchema schema;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return schema;
    schema.fileSize = f.size();

    QTextStream ts(&f);
    QStringList sampleLines;
    int totalRows = 0;
    while (!ts.atEnd() && totalRows < maxScanRows) {
        QString line = ts.readLine();
        if (line.isEmpty() && totalRows == 0) continue;
        sampleLines.append(line);
        ++totalRows;
    }
    if (sampleLines.isEmpty()) return schema;

    QString warn;
    schema.delimiter = sniffDelimiter(sampleLines, &warn);
    schema.detectionWarning = warn;

    QStringList firstRow = splitCsvLine(sampleLines.first(), schema.delimiter);
    schema.hasHeader = firstRowLooksLikeHeader(firstRow);

    // Build a 2D sample (rows x cols) for type inference.
    QVector<QStringList> dataRows;
    for (int i = (schema.hasHeader ? 1 : 0); i < sampleLines.size(); ++i) {
        QStringList r = splitCsvLine(sampleLines[i], schema.delimiter);
        // Pad / truncate to firstRow column count for stable column indexing.
        while (r.size() < firstRow.size()) r.append(QString());
        if (r.size() > firstRow.size()) r = r.mid(0, firstRow.size());
        dataRows.append(r);
    }

    // Build columns
    schema.columns.reserve(firstRow.size());
    for (int c = 0; c < firstRow.size(); ++c) {
        ColumnInfo ci;
        ci.name = schema.hasHeader && !firstRow[c].trimmed().isEmpty()
            ? firstRow[c].trimmed()
            : QStringLiteral("col_%1").arg(c + 1);
        QStringList colVals;
        QSet<QString> seen;
        for (const QStringList &row : dataRows) {
            if (c >= row.size()) continue;
            const QString &v = row[c];
            colVals.append(v);
            if (v.isEmpty()) ci.nullCount++; else ci.nonNullCount++;
            if (ci.sampleValues.size() < 5 && !v.isEmpty() && !seen.contains(v)) {
                ci.sampleValues.append(v);
                seen.insert(v);
            }
        }
        ci.type = inferType(colVals);
        // Min/max for numeric/date columns.
        if (ci.type == ColumnType::Integer || ci.type == ColumnType::Real) {
            double mn = std::numeric_limits<double>::infinity();
            double mx = -std::numeric_limits<double>::infinity();
            bool any = false;
            for (const QString &v : colVals) {
                if (v.isEmpty()) continue;
                bool ok = false;
                double d = v.toDouble(&ok);
                if (!ok) continue;
                if (d < mn) mn = d;
                if (d > mx) mx = d;
                any = true;
            }
            if (any) {
                ci.sampleMin = (ci.type == ColumnType::Integer)
                    ? QString::number(static_cast<long long>(mn))
                    : QString::number(mn, 'g', 6);
                ci.sampleMax = (ci.type == ColumnType::Integer)
                    ? QString::number(static_cast<long long>(mx))
                    : QString::number(mx, 'g', 6);
            }
        }
        schema.columns.append(ci);
    }

    schema.rowCount = (totalRows < maxScanRows)
        ? totalRows - (schema.hasHeader ? 1 : 0)
        : -1;  // -1 means "more than maxScanRows; full count unknown"
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
    lines.append(QStringLiteral("Schema:"));
    for (const auto &c : schema.columns) {
        QString line = QStringLiteral("  %1 (%2)").arg(c.name, columnTypeName(c.type));
        if (c.nullCount > 0)
            line += QStringLiteral(" — %1 nulls").arg(c.nullCount);
        if (!c.sampleMin.isEmpty())
            line += QStringLiteral(" · range %1 .. %2").arg(c.sampleMin, c.sampleMax);
        if (!c.sampleValues.isEmpty()) {
            QStringList sv = c.sampleValues;
            for (auto &v : sv) {
                if (v.length() > 20) v = v.left(17) + QStringLiteral("...");
            }
            line += QStringLiteral(" · e.g. ") + sv.join(", ");
        }
        lines.append(line);
    }

    // Head + tail
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        QStringList allLines;
        while (!ts.atEnd()) {
            allLines.append(ts.readLine());
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

    // Build CREATE TABLE
    QStringList colDefs;
    QStringList placeholders;
    QStringList columnIdents;
    QSet<QString> usedIdents;
    for (const auto &c : schema.columns) {
        QString ident = sanitizeIdent(c.name);
        QString base = ident;
        int n = 2;
        while (usedIdents.contains(ident)) {
            ident = QStringLiteral("%1_%2").arg(base).arg(n++);
        }
        usedIdents.insert(ident);
        columnIdents.append(ident);
        colDefs.append(QStringLiteral("\"%1\" %2").arg(ident, sqliteTypeFor(c.type)));
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
    int rowsLoaded = 0;
    int rowsRead = 0;
    int badRows = 0;
    {
        QSqlQuery q(db);
        const QString insertSql = QStringLiteral("INSERT INTO csv VALUES (%1)")
            .arg(placeholders.join(", "));
        q.prepare(insertSql);
        while (!ts.atEnd()) {
            QString line = ts.readLine();
            if (rowsRead == 0 && schema.hasHeader) { ++rowsRead; continue; }
            ++rowsRead;
            if (line.isEmpty()) continue;
            QStringList parts = splitCsvLine(line, schema.delimiter);
            while (parts.size() < schema.columns.size()) parts.append(QString());
            if (parts.size() > schema.columns.size()) parts = parts.mid(0, schema.columns.size());
            for (int c = 0; c < schema.columns.size(); ++c) {
                const QString &v = parts[c];
                // Convert per type for SQL value affinity.
                switch (schema.columns[c].type) {
                    case ColumnType::Integer: {
                        if (v.isEmpty()) { q.bindValue(c, QVariant()); }
                        else { bool ok = false; qint64 i = v.toLongLong(&ok);
                               q.bindValue(c, ok ? QVariant(i) : QVariant(v)); }
                        break;
                    }
                    case ColumnType::Real:
                    case ColumnType::Boolean: {
                        if (v.isEmpty()) { q.bindValue(c, QVariant()); }
                        else { bool ok = false; double d = v.toDouble(&ok);
                               q.bindValue(c, ok ? QVariant(d) : QVariant(v)); }
                        break;
                    }
                    default:
                        q.bindValue(c, v.isEmpty() ? QVariant() : QVariant(v));
                }
            }
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
