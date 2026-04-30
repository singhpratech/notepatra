#ifndef NOTEPATRA_CSVANALYST_H
#define NOTEPATRA_CSVANALYST_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.43 — CSV ingestion for the Data Analyst AI mode.
//
// Provides three things:
//   1. Schema detection: delimiter sniff, header probe, per-column type
//      inference (int / float / string / date / datetime / bool).
//   2. Compact preview generation for the AI prompt — schema + first/last
//      N rows, byte-capped — instead of dumping the whole CSV into the
//      context window.
//   3. In-memory SQLite ingestion for the `csv_query` tool. The CSV is
//      loaded into an ad-hoc SQLite connection (table name `csv`,
//      column names matching the header) so the AI can run real SQL
//      against it.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>
#include <QStringList>
#include <QVector>

namespace CsvAnalyst {

enum class ColumnType {
    Integer,
    Real,
    Boolean,
    Date,
    DateTime,
    Text   // catch-all
};

QString columnTypeName(ColumnType t);  // "INTEGER" / "REAL" / "BOOLEAN" / "DATE" / "DATETIME" / "TEXT"
QString sqliteTypeFor(ColumnType t);   // SQLite affinity: INTEGER / REAL / TEXT

struct ColumnInfo {
    QString name;             // header name (or "col_N" if no header detected)
    ColumnType type = ColumnType::Text;
    int    nonNullCount = 0;
    int    nullCount = 0;
    QString sampleMin;        // stringified min (for Integer/Real/Date types)
    QString sampleMax;
    QStringList sampleValues; // up to 5 representative values, deduped
};

struct CsvSchema {
    QChar delimiter = ',';
    bool  hasHeader = true;
    int   rowCount = 0;             // data rows (excluding header), -1 if unknown
    qint64 fileSize = 0;
    QVector<ColumnInfo> columns;
    QString detectionWarning;       // non-fatal note (e.g. "delimiter ambiguous, defaulted to ,")
};

// Read up to `maxScanRows` rows from `filePath` (UTF-8 expected; falls back
// to Latin-1) and infer schema. Cheap — uses streaming parser, no full load.
// `maxScanRows` defaults to 1000 which is enough to confidently type most
// real-world CSVs. Returns an empty schema on read error.
CsvSchema detectSchema(const QString &filePath, int maxScanRows = 1000);

// Build a compact textual preview suitable for inclusion in the AI prompt.
// Includes schema (column names + types), a summary line (rows, file size),
// and N head + N tail rows. Capped at ~4KB by default — important so a
// 1GB CSV doesn't blow the context window.
//
// Returns "" if the file can't be opened.
QString buildPreviewText(const QString &filePath,
                         int headRows = 5,
                         int tailRows = 3,
                         int maxBytes = 4096);

// Open an in-memory SQLite database, create a `csv` table matching the
// detected schema, bulk-load rows from `filePath`. On success, the
// connection is open under *outConnectionName and the caller MUST
// QSqlDatabase::removeDatabase(*outConnectionName) when done.
//
// Caps the load at `maxRows` (default 250k) to stop a runaway file from
// exhausting RAM. Sets *outTruncated to true when it stops early.
//
// Returns false + writes error to *outError on schema-detection or I/O
// failure. SQLite parser is permissive — bad rows are logged into
// *outError as a counter, not aborted.
bool loadIntoSqlite(const QString &filePath,
                    int maxRows,
                    QString *outConnectionName,
                    bool   *outTruncated,
                    QString *outError);

// Heuristic: given a path or chip mime-kind, returns true if it's
// CSV-shaped. Used by AIPanel to decide whether to swap the raw-text
// preview for the smart preview when DataAnalyst mode is on.
bool looksLikeCsv(const QString &pathOrName);

} // namespace CsvAnalyst

#endif // NOTEPATRA_CSVANALYST_H
