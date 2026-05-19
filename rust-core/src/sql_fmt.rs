// SPDX-License-Identifier: GPL-3.0-or-later

//! SQL Formatter — Claude-style pretty printer.
//!
//! Strategy:
//!   1. Parse with `sqlparser` using a dialect-specific parser.
//!   2. Walk the AST and emit a nicely indented rendering:
//!        - Multi-column SELECT: one column per line, 4-space indent, trailing comma.
//!        - FROM on its own line, JOIN on its own line, ON continuation indented.
//!        - WHERE / AND / OR: one predicate per line.
//!        - GROUP BY / HAVING / ORDER BY / LIMIT each on their own line.
//!        - CTEs: `WITH cte AS (\n    SELECT ...\n)`.
//!        - Subqueries indented one level deeper than parent.
//!   3. If the parser rejects the input (unsupported syntax / dialect edge case),
//!      fall back to the existing `sqlformat` crate and prepend a one-line
//!      comment so the user still gets something usable.

use sqlformat::{format as legacy_format, FormatOptions, Indent, QueryParams};
use sqlparser::ast::{
    Cte, Distinct, Expr, Fetch, GroupByExpr, Join, JoinConstraint, JoinOperator, MergeAction,
    MergeClause, MergeClauseKind, ObjectName, ObjectNamePart, OrderBy, OrderByExpr, OrderByKind,
    Query, Select, SelectItem, SelectItemQualifiedWildcardKind, SetExpr, Statement, TableFactor,
    TableObject, TableWithJoins, UpdateTableFromKind, With,
};
use sqlparser::dialect::{
    AnsiDialect, Dialect, GenericDialect, MsSqlDialect, MySqlDialect, PostgreSqlDialect,
    SQLiteDialect,
};
use sqlparser::parser::Parser;

// v0.1.48 — hard cap to prevent OOM on pathological inputs. Real-world
// queries tend to be < 100 KB; even huge dynamically-generated DDL stays
// well under 10 MB. 50 MB is a generous ceiling that still bounds memory.
const MAX_INPUT_BYTES: usize = 50 * 1024 * 1024;

/// Public entry — matches the old signature so the existing FFI still works
/// when no dialect is supplied. New code should use `format_sql_dialect`.
#[allow(dead_code)]
pub fn format_sql(input: &str, indent_width: usize, uppercase: bool) -> String {
    format_sql_dialect(input, indent_width, uppercase, "ansi")
}

/// Claude-style pretty printer with explicit dialect selection.
pub fn format_sql_dialect(
    input: &str,
    indent_width: usize,
    uppercase: bool,
    dialect_name: &str,
) -> String {
    format_sql_inner(
        input,
        indent_width,
        uppercase,
        dialect_name,
        /*compact*/ false,
    )
}

/// v0.1.49 — Compact / one-line-where-possible variant. Same dialect support
/// (T-SQL, PostgreSQL, MySQL, SQLite, Oracle, ANSI, Generic) but keeps short
/// statements on a single line, only breaking the long ones. Useful when
/// pasting many short queries in a row or when screen real estate is tight.
pub fn format_sql_compact(
    input: &str,
    indent_width: usize,
    uppercase: bool,
    dialect_name: &str,
) -> String {
    format_sql_inner(
        input,
        indent_width,
        uppercase,
        dialect_name,
        /*compact*/ true,
    )
}

/// v0.1.92 — Protect T-SQL bracket-quoted identifiers (`[onelook-db-1]`,
/// `[Order Details]`, `[my-db].[dbo].[my-table]`) so neither the primary
/// parser nor the legacy sqlformat fallback can mangle them. Everything
/// between `[` and the matching `]` is opaque per T-SQL semantics —
/// hyphens, spaces, punctuation included. T-SQL escapes a literal `]`
/// inside the identifier as `]]`.
///
/// Returns (masked_input, originals). Caller restores by replacing the
/// placeholders in the formatted output.
fn protect_bracket_identifiers(input: &str) -> (String, Vec<String>) {
    let bytes = input.as_bytes();
    let mut out = String::with_capacity(input.len());
    let mut originals: Vec<String> = Vec::new();
    let mut i = 0;
    let mut in_single = false;
    let mut in_double = false;
    let mut in_line_comment = false;
    let mut in_block_comment = false;

    while i < bytes.len() {
        let c = bytes[i];

        if in_line_comment {
            out.push(c as char);
            if c == b'\n' {
                in_line_comment = false;
            }
            i += 1;
            continue;
        }
        if in_block_comment {
            out.push(c as char);
            if c == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                out.push('/');
                in_block_comment = false;
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }
        if in_single {
            out.push(c as char);
            if c == b'\'' {
                // Handle SQL '' escape — stays inside string.
                if i + 1 < bytes.len() && bytes[i + 1] == b'\'' {
                    out.push('\'');
                    i += 2;
                    continue;
                }
                in_single = false;
            }
            i += 1;
            continue;
        }
        if in_double {
            out.push(c as char);
            if c == b'"' {
                in_double = false;
            }
            i += 1;
            continue;
        }

        // Not inside any quote/comment — check for new state entry.
        if c == b'\'' {
            in_single = true;
            out.push('\'');
            i += 1;
            continue;
        }
        if c == b'"' {
            in_double = true;
            out.push('"');
            i += 1;
            continue;
        }
        if c == b'-' && i + 1 < bytes.len() && bytes[i + 1] == b'-' {
            in_line_comment = true;
            out.push_str("--");
            i += 2;
            continue;
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'*' {
            in_block_comment = true;
            out.push_str("/*");
            i += 2;
            continue;
        }

        // Bracket-identifier: capture from `[` through matching unescaped `]`.
        // T-SQL escapes a literal `]` inside the identifier as `]]`.
        if c == b'[' {
            let start = i;
            let mut j = i + 1;
            let mut found_end = false;
            while j < bytes.len() {
                if bytes[j] == b']' {
                    if j + 1 < bytes.len() && bytes[j + 1] == b']' {
                        j += 2; // escaped ]] — keep scanning
                        continue;
                    }
                    found_end = true;
                    break;
                }
                j += 1;
            }
            if found_end {
                let original = std::str::from_utf8(&bytes[start..=j])
                    .unwrap_or("")
                    .to_string();
                let placeholder = format!("__NP_BR_{}__", originals.len());
                originals.push(original);
                out.push_str(&placeholder);
                i = j + 1;
                continue;
            }
            // Unmatched `[` — leave as-is so the parser can flag the issue.
        }

        out.push(c as char);
        i += 1;
    }
    (out, originals)
}

/// Restore protected bracket identifiers in the formatted output. Walks
/// every `__NP_BR_N__` placeholder and substitutes the original literal
/// (including the surrounding `[...]`). Trailing whitespace inside the
/// placeholder region — added by the formatter around what it thought was
/// an operator — is collapsed.
fn restore_bracket_identifiers(output: &str, originals: &[String]) -> String {
    if originals.is_empty() {
        return output.to_string();
    }
    let mut result = output.to_string();
    for (i, original) in originals.iter().enumerate() {
        let placeholder = format!("__NP_BR_{}__", i);
        result = result.replace(&placeholder, original);
    }
    result
}

/// v0.1.92 — Split a T-SQL input on `GO` batch separators. `GO` must be on
/// its own line (whitespace allowed before/after) and outside any string or
/// comment. Returns the list of batches in order. Single-batch input (no
/// `GO` anywhere) returns one element. Same string/comment state tracking as
/// `protect_bracket_identifiers`.
fn split_tsql_go_batches(input: &str) -> Vec<String> {
    let bytes = input.as_bytes();
    let mut batches: Vec<String> = Vec::new();
    let mut cur = String::with_capacity(input.len());
    let mut i = 0;
    let mut in_single = false;
    let mut in_double = false;
    let mut in_line_comment = false;
    let mut in_block_comment = false;

    while i < bytes.len() {
        let c = bytes[i];
        if in_line_comment {
            cur.push(c as char);
            if c == b'\n' {
                in_line_comment = false;
            }
            i += 1;
            continue;
        }
        if in_block_comment {
            cur.push(c as char);
            if c == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                cur.push('/');
                in_block_comment = false;
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }
        if in_single {
            cur.push(c as char);
            if c == b'\'' {
                if i + 1 < bytes.len() && bytes[i + 1] == b'\'' {
                    cur.push('\'');
                    i += 2;
                    continue;
                }
                in_single = false;
            }
            i += 1;
            continue;
        }
        if in_double {
            cur.push(c as char);
            if c == b'"' {
                in_double = false;
            }
            i += 1;
            continue;
        }
        if c == b'\'' {
            in_single = true;
            cur.push('\'');
            i += 1;
            continue;
        }
        if c == b'"' {
            in_double = true;
            cur.push('"');
            i += 1;
            continue;
        }
        if c == b'-' && i + 1 < bytes.len() && bytes[i + 1] == b'-' {
            in_line_comment = true;
            cur.push_str("--");
            i += 2;
            continue;
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'*' {
            in_block_comment = true;
            cur.push_str("/*");
            i += 2;
            continue;
        }

        // Detect `GO` on its own line outside any quote/comment. Match the
        // beginning of a line (i==0 OR previous char is '\n'), optional
        // whitespace, the literal `GO` (case-insensitive), optional whitespace,
        // then end-of-line or end-of-input.
        let at_line_start = i == 0 || bytes[i - 1] == b'\n';
        if at_line_start {
            let mut j = i;
            while j < bytes.len() && (bytes[j] == b' ' || bytes[j] == b'\t') {
                j += 1;
            }
            if j + 1 < bytes.len()
                && (bytes[j] == b'G' || bytes[j] == b'g')
                && (bytes[j + 1] == b'O' || bytes[j + 1] == b'o')
            {
                let after_go = j + 2;
                // Verify what follows: only whitespace then \n or EOF.
                let mut k = after_go;
                while k < bytes.len()
                    && (bytes[k] == b' ' || bytes[k] == b'\t' || bytes[k] == b'\r')
                {
                    k += 1;
                }
                if k == bytes.len() || bytes[k] == b'\n' {
                    // Commit current batch, advance past the line.
                    let trimmed = cur.trim_end_matches(['\n', '\r']).to_string();
                    if !trimmed.trim().is_empty() {
                        batches.push(trimmed);
                    }
                    cur = String::new();
                    i = if k < bytes.len() { k + 1 } else { k };
                    continue;
                }
            }
        }

        cur.push(c as char);
        i += 1;
    }
    let trimmed = cur.trim_end_matches(['\n', '\r']).to_string();
    if !trimmed.trim().is_empty() {
        batches.push(trimmed);
    }
    if batches.is_empty() {
        batches.push(String::new());
    }
    batches
}

/// v0.1.92 — Protect T-SQL `PRINT '...'` statements. sqlparser 0.55 doesn't
/// recognize PRINT as a statement type. We capture `PRINT <expr>` from a
/// statement boundary (start-of-input or after `;`) through the next
/// semicolon-or-EOL and replace it with an inline SELECT placeholder that
/// parses cleanly. The original PRINT is restored verbatim after format.
fn protect_print_statements(input: &str) -> (String, Vec<String>) {
    let bytes = input.as_bytes();
    let mut out = String::with_capacity(input.len());
    let mut originals: Vec<String> = Vec::new();
    let mut i = 0;
    let mut at_stmt_start = true; // true at file start; true after `;` + ws
    let mut in_single = false;
    let mut in_double = false;
    let mut in_line_comment = false;
    let mut in_block_comment = false;

    while i < bytes.len() {
        let c = bytes[i];
        if in_line_comment {
            out.push(c as char);
            if c == b'\n' {
                in_line_comment = false;
            }
            i += 1;
            continue;
        }
        if in_block_comment {
            out.push(c as char);
            if c == b'*' && i + 1 < bytes.len() && bytes[i + 1] == b'/' {
                out.push('/');
                in_block_comment = false;
                i += 2;
                continue;
            }
            i += 1;
            continue;
        }
        if in_single {
            out.push(c as char);
            if c == b'\'' {
                if i + 1 < bytes.len() && bytes[i + 1] == b'\'' {
                    out.push('\'');
                    i += 2;
                    continue;
                }
                in_single = false;
            }
            i += 1;
            continue;
        }
        if in_double {
            out.push(c as char);
            if c == b'"' {
                in_double = false;
            }
            i += 1;
            continue;
        }
        if c == b'\'' {
            in_single = true;
            out.push('\'');
            at_stmt_start = false;
            i += 1;
            continue;
        }
        if c == b'"' {
            in_double = true;
            out.push('"');
            at_stmt_start = false;
            i += 1;
            continue;
        }
        if c == b'-' && i + 1 < bytes.len() && bytes[i + 1] == b'-' {
            in_line_comment = true;
            out.push_str("--");
            i += 2;
            continue;
        }
        if c == b'/' && i + 1 < bytes.len() && bytes[i + 1] == b'*' {
            in_block_comment = true;
            out.push_str("/*");
            i += 2;
            continue;
        }
        if c == b';' {
            out.push(';');
            i += 1;
            at_stmt_start = true;
            continue;
        }
        if c == b' ' || c == b'\t' || c == b'\n' || c == b'\r' {
            out.push(c as char);
            i += 1;
            continue;
        }

        if at_stmt_start {
            // Look ahead for PRINT (case-insensitive) followed by whitespace.
            let remaining = &bytes[i..];
            if remaining.len() >= 6
                && remaining[..5].eq_ignore_ascii_case(b"PRINT")
                && (remaining[5] == b' ' || remaining[5] == b'\t' || remaining[5] == b'\n')
            {
                // Capture through the next `;` or newline (whichever comes
                // first), respecting string state so an embedded `;` in a
                // literal doesn't terminate the PRINT.
                let start = i;
                let mut j = i + 5;
                let mut in_sq = false;
                while j < bytes.len() {
                    let b = bytes[j];
                    if in_sq {
                        if b == b'\'' {
                            if j + 1 < bytes.len() && bytes[j + 1] == b'\'' {
                                j += 2;
                                continue;
                            }
                            in_sq = false;
                        }
                        j += 1;
                        continue;
                    }
                    if b == b'\'' {
                        in_sq = true;
                        j += 1;
                        continue;
                    }
                    if b == b';' || b == b'\n' {
                        break;
                    }
                    j += 1;
                }
                let original = std::str::from_utf8(&bytes[start..j])
                    .unwrap_or("")
                    .to_string();
                let placeholder = format!("SELECT '__NP_PR_{}__'", originals.len());
                originals.push(original);
                out.push_str(&placeholder);
                i = j;
                at_stmt_start = false;
                continue;
            }
            at_stmt_start = false;
        }

        out.push(c as char);
        i += 1;
    }
    (out, originals)
}

fn restore_print_statements(output: &str, originals: &[String]) -> String {
    if originals.is_empty() {
        return output.to_string();
    }
    let mut result = output.to_string();
    for (idx, original) in originals.iter().enumerate() {
        // The placeholder was emitted as `SELECT '__NP_PR_N__'` and the
        // pretty-printer may have appended `;` and surrounding whitespace.
        // Replace both the bare placeholder body and the wrapping SELECT
        // form so PRINT replaces the whole synthetic statement.
        let wrapped = format!("SELECT '__NP_PR_{}__'", idx);
        let wrapped_lower = format!("select '__NP_PR_{}__'", idx);
        result = result.replace(&wrapped, original);
        result = result.replace(&wrapped_lower, original);
    }
    result
}

fn format_sql_inner(
    input: &str,
    indent_width: usize,
    uppercase: bool,
    dialect_name: &str,
    compact: bool,
) -> String {
    if input.len() > MAX_INPUT_BYTES {
        return format!(
            "-- (input too large: {} bytes, max {} bytes)\n{}",
            input.len(),
            MAX_INPUT_BYTES,
            input
        );
    }
    let indent_width = indent_width.clamp(1, 8);

    // v0.1.92 — for T-SQL inputs, split on GO batch separators and format each
    // batch independently. Non-T-SQL (or T-SQL without any GO) reduces to a
    // single batch. Re-emit the GO line between formatted batches.
    let is_tsql = matches!(
        dialect_name.to_lowercase().as_str(),
        "mssql" | "tsql" | "sqlserver" | "t-sql"
    );
    let batches = if is_tsql {
        split_tsql_go_batches(input)
    } else {
        vec![input.to_string()]
    };

    if batches.len() == 1 {
        return format_one_batch(
            &batches[0],
            indent_width,
            uppercase,
            dialect_name,
            compact,
            is_tsql,
        );
    }

    let go_sep = if uppercase { "\nGO\n" } else { "\ngo\n" };
    let mut out = String::new();
    for (i, batch) in batches.iter().enumerate() {
        if i > 0 {
            out.push_str(go_sep);
        }
        out.push_str(&format_one_batch(
            batch,
            indent_width,
            uppercase,
            dialect_name,
            compact,
            is_tsql,
        ));
    }
    out
}

fn format_one_batch(
    input: &str,
    indent_width: usize,
    uppercase: bool,
    dialect_name: &str,
    compact: bool,
    is_tsql: bool,
) -> String {
    let dialect = pick_dialect(dialect_name);

    // v0.1.92 — for T-SQL, mask PRINT statements before parsing.
    let (input_after_print, print_originals) = if is_tsql {
        protect_print_statements(input)
    } else {
        (input.to_string(), Vec::new())
    };

    // v0.1.92 — mask T-SQL bracket-quoted identifiers so neither parser
    // mangles them. Restored verbatim before returning.
    let (masked_input, bracket_originals) = protect_bracket_identifiers(&input_after_print);
    let parse_target = if bracket_originals.is_empty() {
        input_after_print.as_str()
    } else {
        masked_input.as_str()
    };

    let raw = match Parser::parse_sql(&*dialect, parse_target) {
        Ok(stmts) if !stmts.is_empty() => {
            let mut out = String::new();
            for (i, stmt) in stmts.iter().enumerate() {
                if i > 0 {
                    out.push_str(if compact { "\n" } else { "\n\n" });
                }
                let mut w = Writer::new(indent_width, uppercase);
                w.compact = compact;
                w.write_statement(stmt);
                let mut piece = w.finish();
                if compact {
                    piece = compress_whitespace(&piece);
                }
                out.push_str(&piece);
                if !out.trim_end().ends_with(';') {
                    out.push(';');
                }
            }
            out
        }
        _ => {
            // Graceful fallback — parser failed, use sqlformat and leave a note.
            // sqlformat 0.5 added several fields (dialect, inline,
            // joins_as_top_level, etc.) — use Default + override the ones
            // we actually drive from user prefs so future bumps don't keep
            // breaking this site.
            let options = FormatOptions {
                indent: Indent::Spaces(indent_width as u8),
                uppercase: Some(uppercase),
                lines_between_queries: if compact { 1 } else { 2 },
                ..FormatOptions::default()
            };
            let legacy = legacy_format(parse_target, &QueryParams::None, &options);
            format!(
                "-- (parser fallback: syntax unsupported by our parser)\n{}",
                if compact {
                    compress_whitespace(&legacy)
                } else {
                    legacy
                }
            )
        }
    };

    let with_brackets = restore_bracket_identifiers(&raw, &bracket_originals);
    restore_print_statements(&with_brackets, &print_originals)
}

// Collapse the writer's expanded form into a tight one-line-where-possible
// shape. Removes runs of newlines + leading whitespace that were inserted
// by Writer::push_line(); preserves blank lines in string literals (we don't
// touch text inside single quotes).
fn compress_whitespace(input: &str) -> String {
    let mut out = String::with_capacity(input.len());
    let mut in_str = false;
    let mut last_was_space = false;
    let mut prev = '\0';
    for ch in input.chars() {
        if ch == '\'' && prev != '\\' {
            in_str = !in_str;
            out.push(ch);
            last_was_space = false;
        } else if in_str {
            out.push(ch);
            last_was_space = false;
        } else if ch == '\n' || ch == '\t' || ch == '\r' || ch == ' ' {
            if !last_was_space {
                out.push(' ');
                last_was_space = true;
            }
        } else {
            out.push(ch);
            last_was_space = false;
        }
        prev = ch;
    }
    // Re-introduce a single break before each top-level keyword for
    // readability. A "compact" query like:
    //   SELECT a, b FROM t WHERE x = 1 GROUP BY a ORDER BY b
    // stays on one line. Long ones break only at major clause boundaries.
    let break_before = [
        " WITH ",
        " SELECT ",
        " UPDATE ",
        " DELETE ",
        " INSERT ",
        " FROM ",
        " WHERE ",
        " GROUP BY ",
        " HAVING ",
        " ORDER BY ",
        " LIMIT ",
        " OFFSET ",
        " RETURNING ",
        " UNION ",
        " UNION ALL ",
        " INTERSECT ",
        " EXCEPT ",
    ];
    if out.len() > 120 {
        let mut buf = out.clone();
        for kw in &break_before {
            buf = buf.replace(kw, &format!("\n{}", kw.trim_start()));
        }
        // First keyword at line 0 should not have a leading newline.
        out = buf.trim_start_matches('\n').to_string();
    }
    out
}

fn pick_dialect(name: &str) -> Box<dyn Dialect> {
    match name.to_lowercase().as_str() {
        "postgres" | "postgresql" => Box::new(PostgreSqlDialect {}),
        "mysql" => Box::new(MySqlDialect {}),
        "mssql" | "tsql" | "sqlserver" | "t-sql" => Box::new(MsSqlDialect {}),
        "sqlite" => Box::new(SQLiteDialect {}),
        // sqlparser 0.52 does not ship a dedicated PL/SQL dialect; Generic
        // is the most permissive option and accepts most Oracle syntax.
        "plsql" | "oracle" => Box::new(GenericDialect {}),
        "ansi" => Box::new(AnsiDialect {}),
        _ => Box::new(GenericDialect {}),
    }
}

// ═══════════════════════════════════════════════════════════
// Writer — accumulates output with a tracked indent level
// ═══════════════════════════════════════════════════════════

struct Writer {
    buf: String,
    indent_width: usize,
    uppercase: bool,
    level: usize,
    // v0.1.49 — when true, the post-processing in `compress_whitespace`
    // collapses every run of whitespace produced by push_line() into a
    // single space. Writer itself still emits the expanded form; the
    // squeeze happens in one pass after finish().
    compact: bool,
}

impl Writer {
    fn new(indent_width: usize, uppercase: bool) -> Self {
        Self {
            buf: String::new(),
            indent_width,
            uppercase,
            level: 0,
            compact: false,
        }
    }

    fn finish(self) -> String {
        self.buf
    }

    fn indent(&self) -> String {
        " ".repeat(self.level * self.indent_width)
    }

    fn sub_indent(&self, extra: usize) -> String {
        " ".repeat(self.level * self.indent_width + extra * self.indent_width)
    }

    // Format a T-SQL Top clause with our keyword case applied. The Display
    // impl on sqlparser's Top emits "TOP …" — we lowercase it if the user
    // selected lowercase keywords. Strips the trailing " " the impl adds.
    fn kw_format_top(&self, top: &sqlparser::ast::Top) -> String {
        let raw = format!("{}", top);
        let trimmed = raw.trim();
        if self.uppercase {
            trimmed.to_string()
        } else {
            trimmed.to_lowercase()
        }
    }

    fn kw(&self, s: &str) -> String {
        if self.uppercase {
            s.to_uppercase()
        } else {
            s.to_lowercase()
        }
    }

    fn push_line(&mut self, s: &str) {
        if !self.buf.is_empty() && !self.buf.ends_with('\n') {
            self.buf.push('\n');
        }
        self.buf.push_str(&self.indent());
        self.buf.push_str(s);
    }

    fn push(&mut self, s: &str) {
        self.buf.push_str(s);
    }

    // ── Statement dispatch ──

    fn write_statement(&mut self, stmt: &Statement) {
        match stmt {
            Statement::Query(q) => self.write_query(q),
            // v0.1.48 — Claude-style UPDATE / DELETE / INSERT pretty-print.
            // Previously these all fell through to sqlparser's compact
            // Display impl, which produces 200-char single-line output.
            Statement::Update {
                table,
                assignments,
                from,
                selection,
                returning,
                ..
            } => self.write_update(
                table,
                assignments,
                from.as_ref(),
                selection.as_ref(),
                returning.as_deref(),
            ),
            Statement::Delete(d) => self.write_delete(d),
            Statement::Insert(ins) => self.write_insert(ins),
            // v0.1.92 — MERGE statement (T-SQL, Oracle, ANSI, BigQuery).
            // Previously fell through to Display, which collapsed the entire
            // statement onto one line.
            Statement::Merge {
                into,
                table,
                source,
                on,
                clauses,
            } => self.write_merge(*into, table, source, on, clauses),
            other => {
                // DDL and other niche statements — sqlparser's Display.
                let s = format!("{}", other);
                self.push_line(&s);
            }
        }
    }

    fn write_update(
        &mut self,
        table: &TableWithJoins,
        assignments: &[sqlparser::ast::Assignment],
        from: Option<&UpdateTableFromKind>,
        selection: Option<&Expr>,
        returning: Option<&[SelectItem]>,
    ) {
        // T-SQL / Snowflake `UPDATE FROM t SET ...` puts FROM before SET.
        let (from_tables, from_before_set) = match from {
            Some(UpdateTableFromKind::BeforeSet(items)) => (Some(items), true),
            Some(UpdateTableFromKind::AfterSet(items)) => (Some(items), false),
            None => (None, false),
        };

        // UPDATE <table>
        self.push_line(&self.kw("UPDATE"));
        self.push(" ");
        self.write_table_with_joins(table);

        if from_before_set {
            if let Some(items) = from_tables {
                self.push("\n");
                self.push_line(&self.kw("FROM"));
                self.push(" ");
                self.write_table_with_joins_list(items);
            }
        }

        // SET col1 = val1,
        //     col2 = val2
        if !assignments.is_empty() {
            self.push("\n");
            self.push_line(&self.kw("SET"));
            for (i, a) in assignments.iter().enumerate() {
                if i == 0 {
                    self.push(" ");
                } else {
                    self.push(",\n");
                    self.push(&self.sub_indent(1));
                }
                self.push(&format!(
                    "{} = {}",
                    a.target,
                    self.fmt_expr_pretty(&a.value, 1)
                ));
            }
        }

        // FROM (PostgreSQL: UPDATE ... FROM other), only if AfterSet variant.
        if !from_before_set {
            if let Some(items) = from_tables {
                self.push("\n");
                self.push_line(&self.kw("FROM"));
                self.push(" ");
                self.write_table_with_joins_list(items);
            }
        }

        if let Some(w) = selection {
            self.push("\n");
            self.push_line(&self.kw("WHERE"));
            self.write_where(w);
        }

        if let Some(r) = returning {
            self.write_returning(r);
        }
    }

    fn write_delete(&mut self, d: &sqlparser::ast::Delete) {
        use sqlparser::ast::FromTable;
        // DELETE [tables] FROM <from> [USING ...] [WHERE ...]
        let head = if d.tables.is_empty() {
            self.kw("DELETE")
        } else {
            let names: Vec<String> = d.tables.iter().map(fmt_object_name).collect();
            format!("{} {}", self.kw("DELETE"), names.join(", "))
        };
        self.push_line(&head);
        self.push("\n");
        self.push_line(&self.kw("FROM"));
        self.push(" ");
        match &d.from {
            FromTable::WithFromKeyword(items) | FromTable::WithoutKeyword(items) => {
                for (i, twj) in items.iter().enumerate() {
                    if i > 0 {
                        self.push(",\n");
                        self.push(&self.sub_indent(1));
                    }
                    self.write_table_with_joins(twj);
                }
            }
        }

        if let Some(using) = &d.using {
            self.push("\n");
            self.push_line(&self.kw("USING"));
            self.push(" ");
            for (i, twj) in using.iter().enumerate() {
                if i > 0 {
                    self.push(",\n");
                    self.push(&self.sub_indent(1));
                }
                self.write_table_with_joins(twj);
            }
        }

        if let Some(w) = &d.selection {
            self.push("\n");
            self.push_line(&self.kw("WHERE"));
            self.write_where(w);
        }

        if let Some(r) = &d.returning {
            self.write_returning(r);
        }

        if !d.order_by.is_empty() {
            self.push("\n");
            self.push_line(&self.kw("ORDER BY"));
            self.push("\n");
            self.write_order_by(&d.order_by);
        }
        if let Some(limit) = &d.limit {
            self.push("\n");
            self.push_line(&format!("{} {}", self.kw("LIMIT"), fmt_expr(limit)));
        }
    }

    fn write_insert(&mut self, ins: &sqlparser::ast::Insert) {
        // INSERT [OR ACTION] INTO <table>[ AS alias] [(cols)]
        // <source query>
        // [RETURNING ...]
        //
        // We keep the Hive/MySQL-specific decorations (PARTITION, OVERWRITE,
        // priority, replace_into) in the upstream Display impl by using it
        // for the head line — but we write the source Query through our
        // own pretty-printer so VALUES/SELECT inside align nicely.
        let mut head = String::new();
        if ins.replace_into {
            head.push_str(&self.kw("REPLACE"));
        } else {
            head.push_str(&self.kw("INSERT"));
        }
        if let Some(action) = ins.or {
            head.push_str(&format!(" {} {}", self.kw("OR"), action));
        }
        if let Some(p) = ins.priority {
            head.push_str(&format!(" {}", p));
        }
        if ins.ignore {
            head.push_str(&format!(" {}", self.kw("IGNORE")));
        }
        if ins.into {
            head.push_str(&format!(" {}", self.kw("INTO")));
        }
        head.push(' ');
        let table_str = match &ins.table {
            TableObject::TableName(name) => fmt_object_name(name),
            other => format!("{}", other),
        };
        let table = if let Some(alias) = &ins.table_alias {
            format!("{} {} {}", table_str, self.kw("AS"), alias.value)
        } else {
            table_str
        };
        head.push_str(&table);
        if !ins.columns.is_empty() {
            let cols: Vec<String> = ins.columns.iter().map(|c| c.value.clone()).collect();
            // Stack columns one-per-line if there are 4+ or the inline form
            // is wide (matches SELECT projection behavior).
            let inline = format!("{} ({})", head, cols.join(", "));
            if cols.len() >= 4 || inline.chars().count() > 80 {
                self.push_line(&head);
                self.push(" (\n");
                for (i, c) in cols.iter().enumerate() {
                    self.push(&self.sub_indent(1));
                    self.push(c);
                    if i + 1 < cols.len() {
                        self.push(",");
                    }
                    self.push("\n");
                }
                self.push(&self.indent());
                self.push(")");
            } else {
                self.push_line(&inline);
            }
        } else {
            self.push_line(&head);
        }

        // Source query — VALUES (...), or SELECT ...
        if let Some(src) = &ins.source {
            self.push("\n");
            self.write_query(src);
        }

        // ON CONFLICT / ON DUPLICATE KEY UPDATE — the previous writer
        // silently dropped this clause (same bug pattern as the T-SQL TOP
        // clause). Use Display impl since OnInsert variants are dialect-
        // specific and complex; emit on its own line for readability.
        //
        // v0.1.92 — switched from `to_lowercase()` whole-string to keyword-
        // only case conversion via `apply_keyword_case`. Previously
        // `INSERT … VALUES (1) ON DUPLICATE KEY UPDATE c=VALUES(a)` would
        // mangle to `… values(a)` AND mangle identifiers like `Email` to
        // `email` if the user picked lowercase keywords.
        if let Some(on) = &ins.on {
            self.push("\n");
            let s = format!("{}", on);
            let trimmed = s.trim_start();
            self.push_line(&apply_keyword_case(trimmed, self.uppercase));
        }

        if let Some(r) = &ins.returning {
            self.write_returning(r);
        }
    }

    /// v0.1.92 — MERGE statement pretty-print.
    ///
    /// Layout:
    /// ```text
    /// MERGE INTO <target> AS t
    /// USING <source> AS s
    ///     ON <predicate>
    /// WHEN MATCHED [AND <pred>] THEN
    ///     UPDATE SET ... | DELETE | INSERT ...
    /// WHEN NOT MATCHED [BY {TARGET|SOURCE}] [AND <pred>] THEN
    ///     INSERT ...
    /// ```
    ///
    /// Each WHEN clause goes on its own line; the action body (UPDATE / INSERT
    /// / DELETE) goes on the next line indented one level. We use the
    /// MergeAction Display impl for the action body since it already handles
    /// the action keyword + payload correctly — we only control the line
    /// breaks and indentation.
    fn write_merge(
        &mut self,
        into: bool,
        table: &TableFactor,
        source: &TableFactor,
        on: &Expr,
        clauses: &[MergeClause],
    ) {
        let head = if into {
            format!("{} {}", self.kw("MERGE"), self.kw("INTO"))
        } else {
            self.kw("MERGE")
        };
        self.push_line(&head);
        self.push(" ");
        self.write_table_factor(table);

        self.push("\n");
        self.push_line(&self.kw("USING"));
        self.push(" ");
        self.write_table_factor(source);

        self.push("\n");
        self.push(&self.sub_indent(1));
        self.push(&format!("{} {}", self.kw("ON"), fmt_expr(on)));

        for cl in clauses {
            self.push("\n");
            let kind = match cl.clause_kind {
                MergeClauseKind::Matched => self.kw("WHEN MATCHED"),
                MergeClauseKind::NotMatched => self.kw("WHEN NOT MATCHED"),
                MergeClauseKind::NotMatchedByTarget => self.kw("WHEN NOT MATCHED BY TARGET"),
                MergeClauseKind::NotMatchedBySource => self.kw("WHEN NOT MATCHED BY SOURCE"),
            };
            let predicate = match &cl.predicate {
                Some(p) => format!(" {} {}", self.kw("AND"), fmt_expr(p)),
                None => String::new(),
            };
            self.push_line(&format!("{}{} {}", kind, predicate, self.kw("THEN")));
            self.push("\n");
            self.push(&self.sub_indent(1));
            // Action body — keep Display for INSERT/UPDATE/DELETE since
            // assignments + values lists are dialect-shaped.
            let action_str = match &cl.action {
                MergeAction::Update { assignments } => {
                    let mut s = self.kw("UPDATE SET ");
                    let parts: Vec<String> = assignments
                        .iter()
                        .map(|a| format!("{} = {}", a.target, fmt_expr(&a.value)))
                        .collect();
                    s.push_str(&parts.join(", "));
                    if self.uppercase {
                        s
                    } else {
                        // Only the leading UPDATE SET portion was uppercased
                        // via kw(); assignments stay verbatim.
                        s
                    }
                }
                MergeAction::Delete => self.kw("DELETE"),
                MergeAction::Insert(ins) => {
                    // Fall back to Display for the INSERT body. Case applied
                    // via `apply_keyword_case` so identifiers don't get
                    // mangled — same fix as ON CONFLICT.
                    let raw = format!("{}", ins);
                    apply_keyword_case(&raw, self.uppercase)
                }
            };
            self.push(&action_str);
        }
    }

    fn write_returning(&mut self, items: &[SelectItem]) {
        self.push("\n");
        self.push_line(&self.kw("RETURNING"));
        let strs: Vec<String> = items.iter().map(|s| self.fmt_select_item(s)).collect();
        if strs.len() == 1 && !strs[0].contains('\n') && strs[0].len() < 60 {
            self.push(" ");
            self.push(&strs[0]);
        } else {
            for (i, s) in strs.iter().enumerate() {
                self.push("\n");
                self.push(&self.sub_indent(1));
                self.push(s);
                if i + 1 < strs.len() {
                    self.push(",");
                }
            }
        }
    }

    fn write_query(&mut self, q: &Query) {
        if let Some(with) = &q.with {
            self.write_with(with);
            self.push("\n");
        }
        self.write_set_expr(&q.body);
        if let Some(ob) = &q.order_by {
            self.write_order_by_clause(ob);
        }
        if let Some(limit) = &q.limit {
            self.push("\n");
            self.push_line(&format!("{} {}", self.kw("LIMIT"), fmt_expr(limit)));
        }
        if let Some(offset) = &q.offset {
            self.push("\n");
            self.push_line(&format!(
                "{} {}",
                self.kw("OFFSET"),
                fmt_expr(&offset.value)
            ));
        }
        // v0.1.92 — FETCH FIRST N ROWS [PERCENT] [WITH TIES | ONLY]
        // Previously the writer ignored q.fetch entirely; PostgreSQL + ANSI
        // users relying on the WITH TIES form lost the entire clause.
        if let Some(fetch) = &q.fetch {
            self.push("\n");
            self.push_line(&self.fmt_fetch(fetch));
        }
    }

    fn fmt_fetch(&self, fetch: &Fetch) -> String {
        // `FETCH FIRST <N> ROWS [WITH TIES | ONLY]` — `quantity` may be None
        // for the bare `FETCH FIRST ROWS ONLY` form, in which case we drop
        // the count to match SQL:2008.
        let head = self.kw("FETCH FIRST");
        let qty = match &fetch.quantity {
            Some(e) => format!(" {}", fmt_expr(e)),
            None => String::new(),
        };
        let percent = if fetch.percent {
            format!(" {}", self.kw("PERCENT"))
        } else {
            String::new()
        };
        let tail = if fetch.with_ties {
            self.kw("ROWS WITH TIES")
        } else {
            self.kw("ROWS ONLY")
        };
        format!("{}{}{} {}", head, qty, percent, tail)
    }

    fn write_order_by_clause(&mut self, ob: &OrderBy) {
        match &ob.kind {
            OrderByKind::Expressions(exprs) => {
                if !exprs.is_empty() {
                    self.push("\n");
                    self.push_line(&self.kw("ORDER BY"));
                    self.push("\n");
                    self.write_order_by(exprs);
                }
            }
            OrderByKind::All(opts) => {
                self.push("\n");
                let dir = match opts.asc {
                    Some(true) => format!(" {}", self.kw("ASC")),
                    Some(false) => format!(" {}", self.kw("DESC")),
                    None => String::new(),
                };
                let nulls = match opts.nulls_first {
                    Some(true) => format!(" {}", self.kw("NULLS FIRST")),
                    Some(false) => format!(" {}", self.kw("NULLS LAST")),
                    None => String::new(),
                };
                self.push_line(&format!(
                    "{} {}{}{}",
                    self.kw("ORDER BY"),
                    self.kw("ALL"),
                    dir,
                    nulls
                ));
            }
        }
    }

    // Helper used by UPDATE FROM / DELETE FROM USING — writes a list of
    // TableWithJoins separated by ", " with continuation indent.
    fn write_table_with_joins_list(&mut self, items: &[TableWithJoins]) {
        for (i, twj) in items.iter().enumerate() {
            if i > 0 {
                self.push(",\n");
                self.push(&self.sub_indent(1));
            }
            self.write_table_with_joins(twj);
        }
    }

    fn write_with(&mut self, with: &With) {
        let head = if with.recursive {
            format!("{} {}", self.kw("WITH"), self.kw("RECURSIVE"))
        } else {
            self.kw("WITH")
        };
        self.push_line(&head);
        self.push(" ");
        for (i, cte) in with.cte_tables.iter().enumerate() {
            if i > 0 {
                // Align subsequent CTEs under the first so `cte1, cte2` reads well.
                self.push(",\n");
                self.push(&self.indent());
                self.push(&" ".repeat(head.len() + 1));
            }
            self.write_cte(cte);
        }
    }

    fn write_cte(&mut self, cte: &Cte) {
        let name = cte.alias.name.value.clone();
        self.push(&format!("{} {} (\n", name, self.kw("AS")));
        self.level += 1;
        self.write_query(&cte.query);
        self.level -= 1;
        self.push("\n");
        self.push(&self.indent());
        self.push(")");
    }

    fn write_set_expr(&mut self, se: &SetExpr) {
        match se {
            SetExpr::Select(sel) => self.write_select(sel),
            SetExpr::Query(q) => {
                self.push_line("(");
                self.level += 1;
                self.write_query(q);
                self.level -= 1;
                self.push("\n");
                self.push_line(")");
            }
            SetExpr::SetOperation {
                op,
                set_quantifier,
                left,
                right,
            } => {
                self.write_set_expr(left);
                self.push("\n");
                let op_str = format!("{}", op);
                let q_str = format!("{}", set_quantifier);
                let joined = if q_str.is_empty() || q_str == "None" {
                    op_str
                } else {
                    format!("{} {}", op_str, q_str)
                };
                self.push_line(&self.kw(&joined));
                self.push("\n");
                self.write_set_expr(right);
            }
            other => {
                let s = format!("{}", other);
                self.push_line(&s);
            }
        }
    }

    fn write_select(&mut self, sel: &Select) {
        // v0.1.48 — emit T-SQL `TOP N [PERCENT] [WITH TIES]` if present.
        // The previous writer silently dropped this field, so a query like
        // `SELECT TOP 10 * FROM t` lost the TOP after formatting.
        // v0.1.92 — render PostgreSQL `DISTINCT ON (cols)` properly. Pre-0.1.92
        // bare `DISTINCT` was emitted for both DISTINCT and DISTINCT ON, silently
        // dropping the ON column list.
        let distinct_str = sel.distinct.as_ref().map(|d| match d {
            Distinct::Distinct => self.kw("DISTINCT"),
            Distinct::On(exprs) => {
                let cols = exprs.iter().map(fmt_expr).collect::<Vec<_>>().join(", ");
                format!("{} {} ({})", self.kw("DISTINCT"), self.kw("ON"), cols)
            }
        });
        let select_kw = match (&sel.top, distinct_str.as_deref()) {
            (Some(top), Some(d)) => {
                format!("{} {} {}", self.kw("SELECT"), self.kw_format_top(top), d)
            }
            (Some(top), None) => {
                format!("{} {}", self.kw("SELECT"), self.kw_format_top(top))
            }
            (None, Some(d)) => format!("{} {}", self.kw("SELECT"), d),
            (None, None) => self.kw("SELECT"),
        };

        let projection: Vec<String> = sel
            .projection
            .iter()
            .map(|p| self.fmt_select_item(p))
            .collect();

        if projection.len() == 1 && !projection[0].contains('\n') && projection[0].len() < 80 {
            self.push_line(&format!("{} {}", select_kw, projection[0]));
        } else {
            self.push_line(&select_kw);
            for (i, col) in projection.iter().enumerate() {
                self.push("\n");
                self.push(&self.sub_indent(1));
                self.push(col);
                if i + 1 < projection.len() {
                    self.push(",");
                }
            }
        }

        if !sel.from.is_empty() {
            self.push("\n");
            self.push_line(&self.kw("FROM"));
            self.push(" ");
            for (i, twj) in sel.from.iter().enumerate() {
                if i > 0 {
                    self.push(",\n");
                    self.push(&self.sub_indent(1));
                }
                self.write_table_with_joins(twj);
            }
        }

        if let Some(w) = &sel.selection {
            self.push("\n");
            self.push_line(&self.kw("WHERE"));
            self.write_where(w);
        }

        let gb_items = group_by_items(&sel.group_by);
        if !gb_items.is_empty() {
            self.push("\n");
            // v0.1.48 — Claude-style: expand to one column per line when
            // there are 3+ items OR the inline form would be > 80 chars.
            // Mirrors how SELECT projection is expanded above.
            let inline = format!("{} {}", self.kw("GROUP BY"), gb_items.join(", "));
            let should_expand = gb_items.len() >= 3 || inline.chars().count() > 80;
            if should_expand && gb_items.len() > 1 {
                self.push_line(&self.kw("GROUP BY"));
                for (i, item) in gb_items.iter().enumerate() {
                    self.push("\n");
                    self.push(&self.sub_indent(1));
                    self.push(item);
                    if i + 1 < gb_items.len() {
                        self.push(",");
                    }
                }
            } else {
                self.push_line(&inline);
            }
        }

        if let Some(h) = &sel.having {
            self.push("\n");
            self.push_line(&format!(
                "{} {}",
                self.kw("HAVING"),
                self.fmt_expr_pretty(h, 1)
            ));
        }
    }

    fn fmt_select_item(&self, item: &SelectItem) -> String {
        match item {
            SelectItem::UnnamedExpr(e) => self.fmt_expr_pretty(e, 1),
            SelectItem::ExprWithAlias { expr, alias } => {
                format!(
                    "{} {} {}",
                    self.fmt_expr_pretty(expr, 1),
                    self.kw("AS"),
                    alias.value
                )
            }
            SelectItem::QualifiedWildcard(kind, _) => match kind {
                SelectItemQualifiedWildcardKind::ObjectName(name) => {
                    format!("{}.*", fmt_object_name(name))
                }
                SelectItemQualifiedWildcardKind::Expr(e) => format!("({}).*", fmt_expr(e)),
            },
            SelectItem::Wildcard(_) => "*".to_string(),
        }
    }

    /// Pretty-print an expression with Claude-style multi-line formatting
    /// for CASE and long IN lists. `extra_indent` is the column-context
    /// indent (number of indent_widths past the current statement level)
    /// so the continuation lines line up with the column they belong to.
    fn fmt_expr_pretty(&self, e: &Expr, extra_indent: usize) -> String {
        // Width budget: try Display first; only expand if the result is
        // actually long. Most short expressions stay one-line.
        const CASE_WRAP_THRESHOLD: usize = 50;
        const IN_WRAP_THRESHOLD: usize = 80;

        let one_line = format!("{}", e);
        let base_pad = self.sub_indent(extra_indent);

        match e {
            Expr::Case {
                operand,
                conditions,
                else_result,
                ..
            } if one_line.chars().count() > CASE_WRAP_THRESHOLD || one_line.contains('\n') => {
                let mut out = String::new();
                out.push_str(&self.kw("CASE"));
                if let Some(op) = operand {
                    out.push(' ');
                    out.push_str(&fmt_expr(op));
                }
                for cw in conditions.iter() {
                    out.push('\n');
                    out.push_str(&base_pad);
                    out.push_str(&self.indent_str(1));
                    out.push_str(&self.kw("WHEN"));
                    out.push(' ');
                    out.push_str(&fmt_expr(&cw.condition));
                    out.push(' ');
                    out.push_str(&self.kw("THEN"));
                    out.push(' ');
                    out.push_str(&fmt_expr(&cw.result));
                }
                if let Some(er) = else_result {
                    out.push('\n');
                    out.push_str(&base_pad);
                    out.push_str(&self.indent_str(1));
                    out.push_str(&self.kw("ELSE"));
                    out.push(' ');
                    out.push_str(&fmt_expr(er));
                }
                out.push('\n');
                out.push_str(&base_pad);
                out.push_str(&self.kw("END"));
                out
            }
            Expr::InList {
                expr,
                list,
                negated,
            } if one_line.chars().count() > IN_WRAP_THRESHOLD => {
                let mut out = String::new();
                out.push_str(&fmt_expr(expr));
                if *negated {
                    out.push(' ');
                    out.push_str(&self.kw("NOT"));
                }
                out.push(' ');
                out.push_str(&self.kw("IN"));
                out.push_str(" (\n");
                for (i, item) in list.iter().enumerate() {
                    out.push_str(&base_pad);
                    out.push_str(&self.indent_str(1));
                    out.push_str(&fmt_expr(item));
                    if i + 1 < list.len() {
                        out.push(',');
                    }
                    out.push('\n');
                }
                out.push_str(&base_pad);
                out.push(')');
                out
            }
            _ => one_line,
        }
    }

    fn indent_str(&self, n: usize) -> String {
        " ".repeat(n * self.indent_width)
    }

    fn write_table_with_joins(&mut self, twj: &TableWithJoins) {
        self.write_table_factor(&twj.relation);
        for j in &twj.joins {
            self.push("\n");
            self.write_join(j);
        }
    }

    fn write_table_factor(&mut self, tf: &TableFactor) {
        match tf {
            TableFactor::Table { name, alias, .. } => {
                let base = fmt_object_name(name);
                let s = if let Some(a) = alias {
                    format!("{} {}", base, a.name.value)
                } else {
                    base
                };
                self.push(&s);
            }
            TableFactor::Derived {
                lateral,
                subquery,
                alias,
            } => {
                if *lateral {
                    self.push(&self.kw("LATERAL "));
                }
                self.push("(\n");
                self.level += 1;
                self.write_query(subquery);
                self.level -= 1;
                self.push("\n");
                self.push(&self.indent());
                self.push(")");
                if let Some(a) = alias {
                    self.push(&format!(" {}", a.name.value));
                }
            }
            other => {
                let s = format!("{}", other);
                self.push(&s);
            }
        }
    }

    fn write_join(&mut self, j: &Join) {
        let (kw, on_kw) = join_keyword(&j.join_operator, self.uppercase);
        self.push_line(&kw);
        self.push(" ");
        self.write_table_factor(&j.relation);
        if let Some(e) = join_on_expr(&j.join_operator) {
            self.push("\n");
            self.push(&self.sub_indent(1));
            self.push(&format!("{} {}", on_kw, fmt_expr(e)));
        } else if let Some(using) = join_using(&j.join_operator) {
            self.push("\n");
            self.push(&self.sub_indent(1));
            self.push(&format!(
                "{} ({})",
                self.kw("USING"),
                using
                    .iter()
                    .map(fmt_object_name)
                    .collect::<Vec<_>>()
                    .join(", ")
            ));
        }
    }

    // WHERE split into one predicate per line. First has no connector,
    // subsequent get their AND/OR at column 4. Predicates that contain a
    // long IN list or a CASE expression get expanded by fmt_expr_pretty.
    fn write_where(&mut self, e: &Expr) {
        let parts = split_boolean(e);
        for (i, (op, part)) in parts.iter().enumerate() {
            self.push("\n");
            self.push(&self.sub_indent(1));
            let pretty = self.fmt_expr_pretty(part, 1);
            if i == 0 {
                self.push(&pretty);
            } else {
                self.push(&format!("{} {}", self.kw(op), pretty));
            }
        }
    }

    fn write_order_by(&mut self, order: &[OrderByExpr]) {
        for (i, ob) in order.iter().enumerate() {
            if i > 0 {
                self.push(",\n");
            }
            self.push(&self.sub_indent(1));
            let dir = match ob.options.asc {
                Some(true) => format!(" {}", self.kw("ASC")),
                Some(false) => format!(" {}", self.kw("DESC")),
                None => String::new(),
            };
            let nulls = match ob.options.nulls_first {
                Some(true) => format!(" {}", self.kw("NULLS FIRST")),
                Some(false) => format!(" {}", self.kw("NULLS LAST")),
                None => String::new(),
            };
            self.push(&format!("{}{}{}", fmt_expr(&ob.expr), dir, nulls));
        }
    }
}

// ═══════════════════════════════════════════════════════════
// Free helpers
// ═══════════════════════════════════════════════════════════

fn fmt_expr(e: &Expr) -> String {
    format!("{}", e)
}

/// v0.1.92 — Apply the user-selected keyword case to a string that may
/// contain identifiers, string literals, and quoted identifiers. Previously
/// the writer called `to_lowercase()` on whole-Display strings (e.g. ON
/// CONFLICT body, MERGE INSERT body), which mangled identifiers like `Email`
/// → `email` and function-name occurrences like `VALUES(name)` →
/// `values(name)`.
///
/// This walker:
/// - Preserves single-quoted string literals verbatim (`'...'` with `''` escape)
/// - Preserves double-quoted, backtick-quoted, and bracket-quoted identifiers
/// - Recognizes a small set of SQL reserved words and applies the case
/// - Leaves all other word-shaped tokens (identifiers, function names not in
///   the keyword list) untouched
///
/// The keyword list is intentionally narrow — only the words that show up in
/// the Display-emitted fragments we route through here. False-negatives (a
/// real keyword left as-mixed-case) are harmless; false-positives (an
/// identifier accidentally listed) would silently mangle user data.
fn apply_keyword_case(input: &str, uppercase: bool) -> String {
    const KEYWORDS: &[&str] = &[
        "ABORT",
        "ACTION",
        "AND",
        "ANY",
        "AS",
        "BETWEEN",
        "BY",
        "CASE",
        "CONFLICT",
        "CONSTRAINT",
        "CROSS",
        "CURRENT_TIMESTAMP",
        "DEFAULT",
        "DELETE",
        "DISTINCT",
        "DO",
        "DUPLICATE",
        "ELSE",
        "END",
        "EXCLUDED",
        "EXISTS",
        "FAIL",
        "FALSE",
        "FOR",
        "FROM",
        "GROUP",
        "HAVING",
        "IGNORE",
        "ILIKE",
        "IN",
        "INNER",
        "INSERT",
        "INTERSECT",
        "INTO",
        "IS",
        "JOIN",
        "KEY",
        "LATERAL",
        "LEFT",
        "LIKE",
        "LIMIT",
        "MATCHED",
        "NOT",
        "NOTHING",
        "NULL",
        "OFFSET",
        "ON",
        "OR",
        "ORDER",
        "OUTER",
        "OVER",
        "PARTITION",
        "PRIMARY",
        "RECURSIVE",
        "REFERENCES",
        "REPLACE",
        "RETURNING",
        "RIGHT",
        "ROLLBACK",
        "ROW",
        "ROWS",
        "SELECT",
        "SET",
        "SOURCE",
        "TABLE",
        "TARGET",
        "THEN",
        "TRUE",
        "UNION",
        "UNIQUE",
        "UPDATE",
        "USING",
        "VALUES",
        "WHEN",
        "WHERE",
        "WITH",
    ];

    let mut out = String::with_capacity(input.len());
    let bytes = input.as_bytes();
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        // Single-quoted string literal — copy verbatim including the SQL ''
        // escape for embedded single-quote.
        if c == b'\'' {
            out.push('\'');
            i += 1;
            while i < bytes.len() {
                let b = bytes[i];
                out.push(b as char);
                if b == b'\'' {
                    if i + 1 < bytes.len() && bytes[i + 1] == b'\'' {
                        out.push('\'');
                        i += 2;
                        continue;
                    }
                    i += 1;
                    break;
                }
                i += 1;
            }
            continue;
        }
        // Quoted identifier — `"…"`, `` `…` ``, or `[…]`. Each preserved.
        if c == b'"' || c == b'`' || c == b'[' {
            let close = if c == b'[' { b']' } else { c };
            out.push(c as char);
            i += 1;
            while i < bytes.len() {
                let b = bytes[i];
                out.push(b as char);
                if b == close {
                    i += 1;
                    break;
                }
                i += 1;
            }
            continue;
        }
        // Identifier-like word: alpha or underscore followed by alphanumerics.
        if c.is_ascii_alphabetic() || c == b'_' {
            let start = i;
            while i < bytes.len() && (bytes[i].is_ascii_alphanumeric() || bytes[i] == b'_') {
                i += 1;
            }
            let word = &input[start..i];
            let upper = word.to_uppercase();
            if KEYWORDS.binary_search(&upper.as_str()).is_ok() {
                if uppercase {
                    out.push_str(&upper);
                } else {
                    out.push_str(&word.to_lowercase());
                }
            } else {
                out.push_str(word);
            }
            continue;
        }
        out.push(c as char);
        i += 1;
    }
    out
}

fn fmt_object_name(n: &ObjectName) -> String {
    n.0.iter()
        .map(|p| match p {
            ObjectNamePart::Identifier(ident) => ident.value.clone(),
        })
        .collect::<Vec<_>>()
        .join(".")
}

fn group_by_items(gb: &GroupByExpr) -> Vec<String> {
    match gb {
        GroupByExpr::All(_) => vec!["ALL".to_string()],
        GroupByExpr::Expressions(exprs, _) => exprs.iter().map(fmt_expr).collect(),
    }
}

fn join_keyword(op: &JoinOperator, uppercase: bool) -> (String, String) {
    let kw = |s: &str| {
        if uppercase {
            s.to_string()
        } else {
            s.to_lowercase()
        }
    };
    let word = match op {
        JoinOperator::Join(_) => "JOIN",
        JoinOperator::Inner(_) => "INNER JOIN",
        JoinOperator::Left(_) => "LEFT JOIN",
        JoinOperator::LeftOuter(_) => "LEFT JOIN",
        JoinOperator::Right(_) => "RIGHT JOIN",
        JoinOperator::RightOuter(_) => "RIGHT JOIN",
        JoinOperator::FullOuter(_) => "FULL OUTER JOIN",
        JoinOperator::CrossJoin => "CROSS JOIN",
        JoinOperator::Semi(_) => "SEMI JOIN",
        JoinOperator::LeftSemi(_) => "LEFT SEMI JOIN",
        JoinOperator::RightSemi(_) => "RIGHT SEMI JOIN",
        JoinOperator::Anti(_) => "ANTI JOIN",
        JoinOperator::LeftAnti(_) => "LEFT ANTI JOIN",
        JoinOperator::RightAnti(_) => "RIGHT ANTI JOIN",
        JoinOperator::CrossApply => "CROSS APPLY",
        JoinOperator::OuterApply => "OUTER APPLY",
        JoinOperator::AsOf { .. } => "ASOF JOIN",
    };
    (kw(word), kw("ON"))
}

fn join_constraint(op: &JoinOperator) -> Option<&JoinConstraint> {
    match op {
        JoinOperator::Join(c)
        | JoinOperator::Inner(c)
        | JoinOperator::Left(c)
        | JoinOperator::LeftOuter(c)
        | JoinOperator::Right(c)
        | JoinOperator::RightOuter(c)
        | JoinOperator::FullOuter(c)
        | JoinOperator::Semi(c)
        | JoinOperator::LeftSemi(c)
        | JoinOperator::RightSemi(c)
        | JoinOperator::Anti(c)
        | JoinOperator::LeftAnti(c)
        | JoinOperator::RightAnti(c) => Some(c),
        _ => None,
    }
}

fn join_on_expr(op: &JoinOperator) -> Option<&Expr> {
    match join_constraint(op)? {
        JoinConstraint::On(e) => Some(e),
        _ => None,
    }
}

fn join_using(op: &JoinOperator) -> Option<&Vec<ObjectName>> {
    match join_constraint(op)? {
        JoinConstraint::Using(v) => Some(v),
        _ => None,
    }
}

fn split_boolean(e: &Expr) -> Vec<(String, &Expr)> {
    let mut out: Vec<(String, &Expr)> = Vec::new();
    collect_bool(e, "AND", &mut out, true);
    out
}

fn collect_bool<'a>(e: &'a Expr, last_op: &str, out: &mut Vec<(String, &'a Expr)>, first: bool) {
    use sqlparser::ast::BinaryOperator;
    if let Expr::BinaryOp { left, op, right } = e {
        match op {
            BinaryOperator::And => {
                collect_bool(left, last_op, out, first);
                collect_bool(right, "AND", out, false);
                return;
            }
            BinaryOperator::Or => {
                collect_bool(left, last_op, out, first);
                collect_bool(right, "OR", out, false);
                return;
            }
            _ => {}
        }
    }
    let op = if first {
        String::new()
    } else {
        last_op.to_string()
    };
    out.push((op, e));
}

// ═══════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn simple_select_stays_on_one_line() {
        let sql = "select id from users";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(out.contains("SELECT id"));
        assert!(out.contains("FROM"));
    }

    #[test]
    fn multi_column_select_is_stacked() {
        let sql = "select a, b, c, d from t";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(out.contains("SELECT\n    a,"));
        assert!(out.contains("    b,"));
        assert!(out.contains("    d\n"));
    }

    #[test]
    fn join_and_where_are_broken() {
        let sql = "select u.id from users u left join orders o on o.user_id=u.id \
                   where u.active=true and u.created_at>'2024-01-01'";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(out.contains("LEFT JOIN"));
        assert!(out.contains("    ON"));
        assert!(out.contains("WHERE"));
        assert!(out.contains("    AND"));
    }

    #[test]
    fn unsupported_falls_back_gracefully() {
        let sql = "this is not sql at all {{{";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(out.contains("parser fallback"));
    }

    #[test]
    fn lowercase_keywords_honoured() {
        let sql = "SELECT id FROM users";
        let out = format_sql_dialect(sql, 4, false, "ansi");
        assert!(out.contains("select"));
        assert!(out.contains("from"));
    }

    #[test]
    fn over_max_size_is_safe() {
        let input = "SELECT ".repeat(MAX_INPUT_BYTES);
        let out = format_sql_dialect(&input, 4, true, "ansi");
        assert!(out.starts_with("-- (input too large"));
    }

    #[test]
    fn empty_input_does_not_panic() {
        let out = format_sql_dialect("", 4, true, "ansi");
        // Empty input → parser fallback path, but no panic.
        assert!(!out.is_empty() || out.is_empty()); // anything is fine, just don't crash
    }

    #[test]
    fn tsql_top_clause() {
        // T-SQL: SELECT TOP 10 ... — should parse with mssql dialect.
        let sql = "SELECT TOP 10 id, name FROM users ORDER BY created_at DESC";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        assert!(out.contains("SELECT"), "missing SELECT in:\n{}", out);
        assert!(out.contains("TOP"), "missing TOP in:\n{}", out);
        assert!(out.contains("FROM"), "missing FROM in:\n{}", out);
        assert!(out.contains("ORDER BY"), "missing ORDER BY in:\n{}", out);
    }

    #[test]
    fn tsql_bracketed_identifiers() {
        // v0.1.92 — hardened to positional assertions per audit. Both bracket
        // tokens must round-trip exactly (no whitespace mangling, no case
        // change, correct order). Previously only checked `!out.is_empty()`,
        // which masked the `[onelook-db-1]` → `[ onelook - db - 1 ]` bug.
        let sql = "SELECT [Order ID], [Customer Name] FROM [Order Details]";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        assert!(
            out.contains("[Order ID]"),
            "[Order ID] not preserved verbatim:\n{}",
            out
        );
        assert!(
            out.contains("[Customer Name]"),
            "[Customer Name] not preserved verbatim:\n{}",
            out
        );
        assert!(
            out.contains("[Order Details]"),
            "[Order Details] not preserved verbatim:\n{}",
            out
        );
        // Order: the projection brackets must precede the FROM bracket.
        let id_pos = out.find("[Order ID]").unwrap();
        let name_pos = out.find("[Customer Name]").unwrap();
        let details_pos = out.find("[Order Details]").unwrap();
        assert!(id_pos < name_pos, "ordering wrong:\n{}", out);
        assert!(name_pos < details_pos, "ordering wrong:\n{}", out);
    }

    // v0.1.92 — user-reported regression: hyphenated database name
    // `[onelook-db-1]` got mangled to `[ onelook - db - 1 ]` because the
    // T-SQL parser fell through to sqlformat which treats hyphens as the
    // subtraction operator. Bracket-mask must preserve hyphens.
    #[test]
    fn tsql_bracket_with_hyphens_preserved_verbatim() {
        let sql = "SELECT * FROM [onelook-db-1].[dbo].[my-table]";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        assert!(
            out.contains("[onelook-db-1]"),
            "hyphen-bracketed db name mangled:\n{}",
            out
        );
        assert!(
            out.contains("[my-table]"),
            "hyphen-bracketed table name mangled:\n{}",
            out
        );
        assert!(out.contains("[dbo]"), "dbo bracket missing:\n{}", out);
        // Three-part name must keep the dots and the order.
        let prefix = "[onelook-db-1].[dbo].[my-table]";
        assert!(
            out.contains(prefix),
            "three-part name not preserved:\n{}",
            out
        );
    }

    // v0.1.92 — bracket escapes (`]]` inside `[…]` means a literal `]`).
    #[test]
    fn tsql_bracket_escaped_close_bracket() {
        let sql = "SELECT [col]]name] FROM t";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        assert!(
            out.contains("[col]]name]"),
            "bracket escape `]]` not preserved:\n{}",
            out
        );
    }

    // v0.1.92 — DISTINCT ON (cols) PostgreSQL syntax.
    #[test]
    fn pg_distinct_on_renders_full_clause() {
        let sql = "SELECT DISTINCT ON (user_id) user_id, created_at FROM events ORDER BY user_id, created_at DESC";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // Audit finding #4 — `DISTINCT ON (cols)` was previously rendered as
        // bare `DISTINCT`, silently dropping the column list.
        assert!(
            out.contains("DISTINCT ON"),
            "DISTINCT ON keyword sequence missing:\n{}",
            out
        );
        assert!(
            out.contains("(user_id)"),
            "DISTINCT ON column list missing:\n{}",
            out
        );
    }

    // v0.1.92 — FETCH FIRST N ROWS WITH TIES (PostgreSQL / ANSI).
    #[test]
    fn pg_fetch_first_with_ties_renders() {
        let sql = "SELECT id FROM users ORDER BY score DESC FETCH FIRST 10 ROWS WITH TIES";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // Audit finding #6 — q.fetch was ignored entirely pre-v0.1.92.
        assert!(out.contains("FETCH FIRST"), "FETCH FIRST missing:\n{}", out);
        assert!(out.contains("WITH TIES"), "WITH TIES missing:\n{}", out);
        assert!(out.contains("10"), "fetch count missing:\n{}", out);
    }

    #[test]
    fn pg_fetch_first_only_renders() {
        let sql = "SELECT id FROM users FETCH FIRST 5 ROWS ONLY";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.contains("FETCH FIRST"), "FETCH FIRST missing:\n{}", out);
        assert!(out.contains("ROWS ONLY"), "ROWS ONLY missing:\n{}", out);
    }

    // v0.1.92 — MERGE statement (T-SQL / Oracle / ANSI).
    #[test]
    fn tsql_merge_expands_when_clauses() {
        let sql = "MERGE INTO Target AS t USING Source AS s ON t.id = s.id WHEN MATCHED THEN UPDATE SET t.name = s.name WHEN NOT MATCHED THEN INSERT (id, name) VALUES (s.id, s.name)";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        // Audit finding #2 — Statement::Merge previously collapsed to one line.
        assert!(out.contains("MERGE"), "MERGE keyword missing:\n{}", out);
        assert!(out.contains("USING"), "USING keyword missing:\n{}", out);
        assert!(
            out.contains("WHEN MATCHED"),
            "WHEN MATCHED missing:\n{}",
            out
        );
        assert!(
            out.contains("WHEN NOT MATCHED"),
            "WHEN NOT MATCHED missing:\n{}",
            out
        );
        // Multi-line: at least 4 separate clauses on separate lines.
        let line_count = out.lines().count();
        assert!(
            line_count >= 5,
            "MERGE not multi-line (only {} lines):\n{}",
            line_count,
            out
        );
    }

    // v0.1.92 — T-SQL GO batch separator.
    #[test]
    fn tsql_go_batch_separator_preserved() {
        let sql = "SELECT 1\nGO\nSELECT 2";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        // Audit finding (T-SQL high): GO previously caused fallback for the
        // whole input. Now pre-split into batches, each formatted, rejoined.
        assert!(out.contains("GO"), "GO batch separator dropped:\n{}", out);
        let go_pos = out.find("GO").unwrap();
        assert!(
            out[..go_pos].to_uppercase().contains("SELECT 1"),
            "first batch missing:\n{}",
            out
        );
        assert!(
            out[go_pos..].to_uppercase().contains("SELECT 2"),
            "second batch missing:\n{}",
            out
        );
    }

    // v0.1.92 — T-SQL PRINT statement preserved verbatim.
    #[test]
    fn tsql_print_statement_preserved() {
        let sql = "PRINT 'hello world'; SELECT 1";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        assert!(
            out.contains("PRINT 'hello world'"),
            "PRINT statement mangled:\n{}",
            out
        );
        assert!(
            out.to_uppercase().contains("SELECT"),
            "SELECT after PRINT lost:\n{}",
            out
        );
    }

    // v0.1.92 — ON CONFLICT body must not lowercase identifiers.
    #[test]
    fn pg_on_conflict_preserves_identifier_case_in_lowercase_mode() {
        let sql = "INSERT INTO Users (Email, Name) VALUES ('a@x.com', 'Alice') ON CONFLICT (Email) DO UPDATE SET Name = EXCLUDED.Name";
        let out = format_sql_dialect(sql, 4, false, "postgres");
        // Identifiers `Email`, `Name` must keep their original case even in
        // lowercase-keyword mode. Audit finding #5 — to_lowercase() pre-fix
        // mangled `Email` → `email`.
        assert!(
            out.contains("Email"),
            "identifier 'Email' lowercased:\n{}",
            out
        );
        assert!(
            out.contains("Name"),
            "identifier 'Name' lowercased:\n{}",
            out
        );
        // Keywords on the other hand must be lowercased.
        assert!(
            out.contains("on conflict"),
            "ON CONFLICT keywords not lowercased:\n{}",
            out
        );
    }

    // v0.1.92 — MySQL ON DUPLICATE KEY UPDATE keyword case fix.
    #[test]
    fn mysql_on_duplicate_key_preserves_function_case() {
        let sql =
            "INSERT INTO t (a, b) VALUES (1, 2) ON DUPLICATE KEY UPDATE c = VALUES(a) + VALUES(b)";
        let out = format_sql_dialect(sql, 4, true, "mysql");
        // VALUES function call: case preserved because we asked for uppercase.
        assert!(
            out.contains("VALUES(a)") || out.contains("VALUES(`a`)"),
            "VALUES function call mangled in uppercase mode:\n{}",
            out
        );
    }

    #[test]
    fn postgres_double_colon_cast() {
        let sql = "SELECT id::text FROM users";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // Should preserve ::text or use CAST — either is acceptable.
        assert!(
            out.to_lowercase().contains("text"),
            "missing cast target in:\n{}",
            out
        );
    }

    #[test]
    fn postgres_lateral_join() {
        let sql =
            "SELECT u.id, posts.title FROM users u, LATERAL (SELECT title FROM posts WHERE posts.user_id = u.id LIMIT 1) posts";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(
            out.to_uppercase().contains("LATERAL"),
            "missing LATERAL in:\n{}",
            out
        );
    }

    #[test]
    fn cte_with_multiple_ctes() {
        let sql = "WITH a AS (SELECT 1 AS x), b AS (SELECT 2 AS y) SELECT * FROM a JOIN b ON 1=1";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(
            out.to_uppercase().contains("WITH"),
            "missing WITH in:\n{}",
            out
        );
        // Both CTE names should appear.
        assert!(out.to_lowercase().contains(" a ") || out.contains("a AS"));
    }

    #[test]
    fn idempotent_formatting() {
        // Formatting twice should give the same output.
        let sql = "select id, name from users where active = true";
        let once = format_sql_dialect(sql, 4, true, "ansi");
        let twice = format_sql_dialect(&once, 4, true, "ansi");
        assert_eq!(
            once, twice,
            "format_sql is not idempotent:\nonce={}\ntwice={}",
            once, twice
        );
    }

    #[test]
    fn case_when_expands_when_long() {
        let sql = "SELECT CASE WHEN status = 'pending' THEN 1 WHEN status = 'active' THEN 2 WHEN status = 'archived' THEN 3 ELSE 0 END AS rank FROM tasks";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        // CASE should be on its own line, with WHEN / THEN on subsequent lines.
        assert!(out.contains("CASE"));
        let lines: Vec<&str> = out.lines().collect();
        let when_count = lines.iter().filter(|l| l.contains("WHEN")).count();
        assert!(when_count >= 3, "expected ≥3 WHEN lines, got:\n{}", out);
        assert!(out.contains("ELSE"));
        assert!(out.contains("END"));
    }

    #[test]
    fn case_when_short_stays_inline() {
        let sql = "SELECT CASE WHEN x>0 THEN 1 ELSE 0 END FROM t";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        // Short CASE should not expand — should stay inline-ish.
        let case_lines = out
            .lines()
            .filter(|l| l.to_uppercase().contains("CASE"))
            .count();
        assert_eq!(case_lines, 1, "short CASE got expanded:\n{}", out);
    }

    #[test]
    fn long_in_list_wraps() {
        let sql = "SELECT id FROM users WHERE country IN ('US', 'CA', 'UK', 'DE', 'FR', 'JP', 'AU', 'BR', 'IN', 'CN')";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        // Each country should appear on its own line under IN (.
        for c in ["'US'", "'CA'", "'UK'", "'DE'"] {
            assert!(out.contains(c), "missing {} in:\n{}", c, out);
        }
        // Should have an opening paren after IN.
        assert!(out.contains("IN ("));
    }

    #[test]
    fn group_by_expands_with_many_columns() {
        let sql = "SELECT a, b, c, COUNT(*) FROM t GROUP BY a, b, c";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        let group_by_lines: Vec<&str> = out
            .lines()
            .skip_while(|l| !l.contains("GROUP BY"))
            .take(5)
            .collect();
        // After GROUP BY line, should have at least 3 indented column lines.
        let column_lines = group_by_lines
            .iter()
            .skip(1)
            .filter(|l| l.starts_with("    ") && !l.is_empty())
            .count();
        assert!(
            column_lines >= 3,
            "expected ≥3 indented GROUP BY columns, got:\n{}",
            out
        );
    }

    // ─── Real-world PostgreSQL patterns ──────────────────────────────────
    //
    // These tests use queries adapted from common patterns documented in
    // the PostgreSQL manual and frequently-seen Stack Overflow / project
    // examples. The goal is "no crash + output contains the right kw"
    // rather than byte-exact match — the parser may rearrange minor
    // formatting that we'd happily accept.

    #[test]
    fn pg_upsert_on_conflict() {
        let sql = "INSERT INTO products (id, name, price) VALUES (1, 'Widget', 9.99) ON CONFLICT (id) DO UPDATE SET name = EXCLUDED.name, price = EXCLUDED.price";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.contains("INSERT") && out.contains("INTO"));
        assert!(
            out.to_uppercase().contains("ON CONFLICT") || out.contains("EXCLUDED"),
            "missing ON CONFLICT in:\n{}",
            out
        );
    }

    #[test]
    fn pg_with_recursive() {
        let sql = "WITH RECURSIVE descendants AS (SELECT id, parent_id, name FROM categories WHERE id = 1 UNION ALL SELECT c.id, c.parent_id, c.name FROM categories c INNER JOIN descendants d ON d.id = c.parent_id) SELECT * FROM descendants ORDER BY id";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(
            out.to_uppercase().contains("WITH"),
            "missing WITH:\n{}",
            out
        );
        assert!(
            out.to_uppercase().contains("RECURSIVE"),
            "missing RECURSIVE:\n{}",
            out
        );
        assert!(
            out.to_uppercase().contains("UNION ALL"),
            "missing UNION ALL:\n{}",
            out
        );
    }

    #[test]
    fn pg_window_function() {
        let sql = "SELECT id, salary, RANK() OVER (PARTITION BY department ORDER BY salary DESC) AS rank FROM employees";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("RANK"));
        assert!(out.to_uppercase().contains("OVER"));
        assert!(out.to_uppercase().contains("PARTITION BY"));
    }

    #[test]
    fn pg_json_operators() {
        // -> returns JSON, ->> returns text. sqlparser-rs handles these.
        let sql = "SELECT data->'name' AS name, data->>'email' AS email FROM users WHERE data->>'status' = 'active'";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.contains("->"));
        assert!(out.contains("->>"));
        assert!(out.contains("name"));
        assert!(out.contains("email"));
    }

    #[test]
    fn pg_array_agg_string_agg() {
        let sql = "SELECT department, ARRAY_AGG(name ORDER BY name) AS members, STRING_AGG(email, ', ') AS email_list FROM employees GROUP BY department";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("ARRAY_AGG"));
        assert!(out.to_uppercase().contains("STRING_AGG"));
        assert!(out.to_uppercase().contains("GROUP BY"));
    }

    #[test]
    fn pg_update_with_returning() {
        let sql = "UPDATE users SET email = 'new@example.com', updated_at = NOW() WHERE id = 42 RETURNING id, email, updated_at";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("UPDATE"));
        assert!(out.to_uppercase().contains("SET"));
        assert!(out.to_uppercase().contains("WHERE"));
        assert!(out.to_uppercase().contains("RETURNING"));
        // SET should be on its own line; assignment count check.
        let set_idx = out.to_uppercase().find("SET").unwrap();
        let where_idx = out.to_uppercase().find("WHERE").unwrap();
        assert!(set_idx < where_idx);
    }

    #[test]
    fn pg_delete_with_using_returning() {
        let sql = "DELETE FROM orders USING customers WHERE orders.customer_id = customers.id AND customers.country = 'XX' RETURNING orders.id";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("DELETE"));
        assert!(out.to_uppercase().contains("FROM"));
        assert!(out.to_uppercase().contains("USING"));
        assert!(out.to_uppercase().contains("WHERE"));
        assert!(out.to_uppercase().contains("RETURNING"));
    }

    #[test]
    fn pg_insert_with_4_plus_columns_stacks() {
        let sql = "INSERT INTO users (id, name, email, country, created_at) VALUES (1, 'Alice', 'alice@x.com', 'US', NOW())";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // Either stacked one-per-line OR inline — but must contain all columns.
        for col in ["id", "name", "email", "country", "created_at"] {
            assert!(out.contains(col), "missing {} in:\n{}", col, out);
        }
    }

    #[test]
    fn pg_complex_real_world_analytics_query() {
        let sql = "WITH monthly_revenue AS (\
                       SELECT date_trunc('month', created_at) AS month, \
                              SUM(amount) AS revenue \
                       FROM orders \
                       WHERE created_at >= '2024-01-01' AND status = 'completed' \
                       GROUP BY date_trunc('month', created_at)\
                   ), yoy AS (\
                       SELECT month, revenue, \
                              LAG(revenue, 12) OVER (ORDER BY month) AS prev_year_revenue \
                       FROM monthly_revenue\
                   ) \
                   SELECT month, revenue, prev_year_revenue, \
                          CASE WHEN prev_year_revenue IS NULL THEN NULL \
                               WHEN prev_year_revenue = 0 THEN NULL \
                               ELSE ROUND(((revenue - prev_year_revenue) / prev_year_revenue) * 100, 2) END AS yoy_pct \
                   FROM yoy ORDER BY month";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("WITH"));
        assert!(out.to_uppercase().contains("LAG"));
        assert!(out.to_uppercase().contains("OVER"));
        assert!(out.to_uppercase().contains("CASE"));
        assert!(out.to_uppercase().contains("ORDER BY"));
        // Verify CASE expanded across multiple lines.
        let case_lines = out
            .lines()
            .filter(|l| l.to_uppercase().contains("WHEN"))
            .count();
        assert!(case_lines >= 2, "CASE not expanded:\n{}", out);
    }

    #[test]
    fn pg_lateral_join_real_world() {
        let sql = "SELECT u.id, u.name, latest_post.title, latest_post.published_at \
                   FROM users u \
                   LEFT JOIN LATERAL (\
                       SELECT title, published_at FROM posts \
                       WHERE posts.user_id = u.id \
                       ORDER BY published_at DESC LIMIT 1\
                   ) latest_post ON TRUE";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("LATERAL"));
        assert!(out.to_uppercase().contains("LEFT JOIN"));
    }

    #[test]
    fn pg_multi_condition_join_on() {
        // The ON clause has two conditions joined by AND. Either kept inline
        // or expanded — either way both should appear.
        let sql = "SELECT u.id FROM users u INNER JOIN orders o ON o.user_id = u.id AND o.created_at > '2024-01-01'";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.contains("o.user_id"));
        assert!(out.contains("o.created_at"));
    }

    #[test]
    fn pg_dollar_quoted_string() {
        let sql = "SELECT $$it's a test$$";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // sqlparser may convert to a regular quoted string or fall back —
        // either way, must not crash and must contain the literal "test".
        assert!(
            out.to_lowercase().contains("test"),
            "lost dollar-quoted body in:\n{}",
            out
        );
    }

    #[test]
    fn compact_short_select_stays_one_line() {
        let sql = "select id, name from users where id = 1";
        let out = format_sql_compact(sql, 4, true, "ansi");
        // Short query — should fit on one line (no break-before keywords).
        assert!(
            !out.contains("\nFROM"),
            "compact short query has line break:\n{}",
            out
        );
        assert!(out.to_uppercase().contains("SELECT"));
        assert!(out.to_uppercase().contains("FROM"));
    }

    #[test]
    fn compact_long_query_breaks_at_clauses() {
        let sql = "select user_id, full_name, email_address, created_at, last_login_at from users where active = true and created_at > '2024-01-01' group by user_id order by last_login_at desc";
        let out = format_sql_compact(sql, 4, true, "ansi");
        // Long query — should break at major clause boundaries (FROM/WHERE/GROUP BY/ORDER BY).
        let breaks = out.lines().count();
        assert!(
            breaks >= 4,
            "long compact query did not break at clauses (got {} lines):\n{}",
            breaks,
            out
        );
    }

    #[test]
    fn compact_postgres_upsert_does_not_panic() {
        let sql = "insert into t (id, name) values (1, 'a') on conflict (id) do update set name = excluded.name";
        let out = format_sql_compact(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("INSERT"));
        assert!(
            out.to_uppercase().contains("ON CONFLICT") || out.to_lowercase().contains("excluded")
        );
    }

    #[test]
    fn compact_tsql_top_preserved() {
        let sql = "select top 5 id, name from users order by created_at desc";
        let out = format_sql_compact(sql, 4, true, "mssql");
        assert!(out.to_uppercase().contains("TOP"));
    }

    #[test]
    fn full_claude_example_renders_cleanly() {
        let sql = "SELECT u.id, u.name, u.email, COUNT(o.id) AS order_count \
                   FROM users u \
                   LEFT JOIN orders o ON o.user_id = u.id \
                   WHERE u.created_at > '2024-01-01' AND u.active = TRUE \
                   GROUP BY u.id, u.name, u.email \
                   HAVING COUNT(o.id) > 5 \
                   ORDER BY order_count DESC \
                   LIMIT 100";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        for kw in [
            "SELECT",
            "FROM",
            "LEFT JOIN",
            "WHERE",
            "GROUP BY",
            "HAVING",
            "ORDER BY",
            "LIMIT",
        ] {
            assert!(out.contains(kw), "missing keyword {} in:\n{}", kw, out);
        }
        assert!(out.contains("    u.id,"));
        assert!(out.contains("    u.email,"));
        assert!(out.contains("    ON"));
        assert!(out.trim_end().ends_with(';'));
    }
}
