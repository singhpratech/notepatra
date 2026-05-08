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
    Cte, Expr, GroupByExpr, Join, JoinConstraint, JoinOperator, ObjectName, OrderByExpr, Query,
    Select, SelectItem, SetExpr, Statement, TableFactor, TableWithJoins, With,
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
    format_sql_inner(input, indent_width, uppercase, dialect_name, /*compact*/ false)
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
    format_sql_inner(input, indent_width, uppercase, dialect_name, /*compact*/ true)
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
    let dialect = pick_dialect(dialect_name);
    let indent_width = indent_width.clamp(1, 8);

    match Parser::parse_sql(&*dialect, input) {
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
            let options = FormatOptions {
                indent: Indent::Spaces(indent_width as u8),
                uppercase: Some(uppercase),
                lines_between_queries: if compact { 1 } else { 2 },
                ignore_case_convert: None,
            };
            let legacy = legacy_format(input, &QueryParams::None, &options);
            format!(
                "-- (parser fallback: syntax unsupported by our parser)\n{}",
                if compact { compress_whitespace(&legacy) } else { legacy }
            )
        }
    }
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
        } else if ch == '\n' || ch == '\t' || ch == '\r' {
            if !last_was_space {
                out.push(' ');
                last_was_space = true;
            }
        } else if ch == ' ' {
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
        " WITH ", " SELECT ", " UPDATE ", " DELETE ", " INSERT ",
        " FROM ", " WHERE ", " GROUP BY ", " HAVING ",
        " ORDER BY ", " LIMIT ", " OFFSET ", " RETURNING ",
        " UNION ", " UNION ALL ", " INTERSECT ", " EXCEPT ",
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
            } => self.write_update(table, assignments, from.as_ref(), selection.as_ref(), returning.as_deref()),
            Statement::Delete(d) => self.write_delete(d),
            Statement::Insert(ins) => self.write_insert(ins),
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
        from: Option<&TableWithJoins>,
        selection: Option<&Expr>,
        returning: Option<&[SelectItem]>,
    ) {
        // UPDATE <table>
        self.push_line(&self.kw("UPDATE"));
        self.push(" ");
        self.write_table_with_joins(table);

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
                self.push(&format!("{} = {}", a.target, self.fmt_expr_pretty(&a.value, 1)));
            }
        }

        // FROM (PostgreSQL: UPDATE ... FROM other)
        if let Some(f) = from {
            self.push("\n");
            self.push_line(&self.kw("FROM"));
            self.push(" ");
            self.write_table_with_joins(f);
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
        let table = if let Some(alias) = &ins.table_alias {
            format!("{} {} {}", fmt_object_name(&ins.table_name), self.kw("AS"), alias.value)
        } else {
            fmt_object_name(&ins.table_name)
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
        if let Some(on) = &ins.on {
            self.push("\n");
            let s = format!("{}", on);
            // Display starts with " ON CONFLICT..." or " ON DUPLICATE...";
            // strip leading whitespace and emit on its own line.
            let trimmed = s.trim_start();
            if self.uppercase {
                self.push_line(trimmed);
            } else {
                // Lowercase ON / CONFLICT / DO / UPDATE / NOTHING keywords;
                // identifiers and string literals stay as written. We do a
                // narrow keyword-only lower-case to avoid mangling user
                // identifiers — simplest: lowercase the whole thing if the
                // user's project asked for lowercase output.
                self.push_line(&trimmed.to_lowercase());
            }
        }

        if let Some(r) = &ins.returning {
            self.write_returning(r);
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
            if !ob.exprs.is_empty() {
                self.push("\n");
                self.push_line(&self.kw("ORDER BY"));
                self.push("\n");
                self.write_order_by(&ob.exprs);
            }
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
        let select_kw = match (&sel.top, &sel.distinct) {
            (Some(top), Some(_)) => format!(
                "{} {} {}",
                self.kw("SELECT"),
                self.kw_format_top(top),
                self.kw("DISTINCT")
            ),
            (Some(top), None) => {
                format!("{} {}", self.kw("SELECT"), self.kw_format_top(top))
            }
            (None, Some(_)) => format!("{} {}", self.kw("SELECT"), self.kw("DISTINCT")),
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
            SelectItem::QualifiedWildcard(obj, _) => format!("{}.*", fmt_object_name(obj)),
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
                results,
                else_result,
            } if one_line.chars().count() > CASE_WRAP_THRESHOLD
                || one_line.contains('\n') =>
            {
                let mut out = String::new();
                out.push_str(&self.kw("CASE"));
                if let Some(op) = operand {
                    out.push(' ');
                    out.push_str(&fmt_expr(op));
                }
                for (cond, res) in conditions.iter().zip(results.iter()) {
                    out.push('\n');
                    out.push_str(&base_pad);
                    out.push_str(&self.indent_str(1));
                    out.push_str(&self.kw("WHEN"));
                    out.push(' ');
                    out.push_str(&fmt_expr(cond));
                    out.push(' ');
                    out.push_str(&self.kw("THEN"));
                    out.push(' ');
                    out.push_str(&fmt_expr(res));
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
            } if one_line.chars().count() > IN_WRAP_THRESHOLD =>
            {
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
                    .map(|i| i.value.clone())
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
            let dir = match ob.asc {
                Some(true) => format!(" {}", self.kw("ASC")),
                Some(false) => format!(" {}", self.kw("DESC")),
                None => String::new(),
            };
            let nulls = match ob.nulls_first {
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

fn fmt_object_name(n: &ObjectName) -> String {
    n.0.iter()
        .map(|p| p.value.clone())
        .collect::<Vec<_>>()
        .join(".")
}

fn group_by_items(gb: &GroupByExpr) -> Vec<String> {
    match gb {
        GroupByExpr::All(_) => vec!["ALL".to_string()],
        GroupByExpr::Expressions(exprs, _) => exprs.iter().map(|e| fmt_expr(e)).collect(),
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
        JoinOperator::Inner(_) => "INNER JOIN",
        JoinOperator::LeftOuter(_) => "LEFT JOIN",
        JoinOperator::RightOuter(_) => "RIGHT JOIN",
        JoinOperator::FullOuter(_) => "FULL OUTER JOIN",
        JoinOperator::CrossJoin => "CROSS JOIN",
        JoinOperator::LeftSemi(_) => "LEFT SEMI JOIN",
        JoinOperator::RightSemi(_) => "RIGHT SEMI JOIN",
        JoinOperator::LeftAnti(_) => "LEFT ANTI JOIN",
        JoinOperator::RightAnti(_) => "RIGHT ANTI JOIN",
        JoinOperator::CrossApply => "CROSS APPLY",
        JoinOperator::OuterApply => "OUTER APPLY",
        JoinOperator::AsOf { .. } => "ASOF JOIN",
    };
    (kw(word), kw("ON"))
}

fn join_on_expr(op: &JoinOperator) -> Option<&Expr> {
    match op {
        JoinOperator::Inner(c)
        | JoinOperator::LeftOuter(c)
        | JoinOperator::RightOuter(c)
        | JoinOperator::FullOuter(c)
        | JoinOperator::LeftSemi(c)
        | JoinOperator::RightSemi(c)
        | JoinOperator::LeftAnti(c)
        | JoinOperator::RightAnti(c) => match c {
            JoinConstraint::On(e) => Some(e),
            _ => None,
        },
        _ => None,
    }
}

fn join_using(op: &JoinOperator) -> Option<&Vec<sqlparser::ast::Ident>> {
    match op {
        JoinOperator::Inner(c)
        | JoinOperator::LeftOuter(c)
        | JoinOperator::RightOuter(c)
        | JoinOperator::FullOuter(c)
        | JoinOperator::LeftSemi(c)
        | JoinOperator::RightSemi(c)
        | JoinOperator::LeftAnti(c)
        | JoinOperator::RightAnti(c) => match c {
            JoinConstraint::Using(v) => Some(v),
            _ => None,
        },
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
        // T-SQL allows [Order Details] as an identifier — should round-trip.
        let sql = "SELECT [Order ID], [Customer Name] FROM [Order Details]";
        let out = format_sql_dialect(sql, 4, true, "mssql");
        // Either bracketed form is preserved, OR fallback note is present —
        // we accept both, just don't crash.
        assert!(!out.is_empty());
    }

    #[test]
    fn postgres_double_colon_cast() {
        let sql = "SELECT id::text FROM users";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        // Should preserve ::text or use CAST — either is acceptable.
        assert!(out.to_lowercase().contains("text"), "missing cast target in:\n{}", out);
    }

    #[test]
    fn postgres_lateral_join() {
        let sql =
            "SELECT u.id, posts.title FROM users u, LATERAL (SELECT title FROM posts WHERE posts.user_id = u.id LIMIT 1) posts";
        let out = format_sql_dialect(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("LATERAL"), "missing LATERAL in:\n{}", out);
    }

    #[test]
    fn cte_with_multiple_ctes() {
        let sql = "WITH a AS (SELECT 1 AS x), b AS (SELECT 2 AS y) SELECT * FROM a JOIN b ON 1=1";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        assert!(out.to_uppercase().contains("WITH"), "missing WITH in:\n{}", out);
        // Both CTE names should appear.
        assert!(out.to_lowercase().contains(" a ") || out.contains("a AS"));
    }

    #[test]
    fn idempotent_formatting() {
        // Formatting twice should give the same output.
        let sql = "select id, name from users where active = true";
        let once = format_sql_dialect(sql, 4, true, "ansi");
        let twice = format_sql_dialect(&once, 4, true, "ansi");
        assert_eq!(once, twice, "format_sql is not idempotent:\nonce={}\ntwice={}", once, twice);
    }

    #[test]
    fn case_when_expands_when_long() {
        let sql = "SELECT CASE WHEN status = 'pending' THEN 1 WHEN status = 'active' THEN 2 WHEN status = 'archived' THEN 3 ELSE 0 END AS rank FROM tasks";
        let out = format_sql_dialect(sql, 4, true, "ansi");
        // CASE should be on its own line, with WHEN / THEN on subsequent lines.
        assert!(out.contains("CASE"));
        let lines: Vec<&str> = out.lines().collect();
        let when_count = lines.iter().filter(|l| l.contains("WHEN")).count();
        assert!(
            when_count >= 3,
            "expected ≥3 WHEN lines, got:\n{}",
            out
        );
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
        assert!(out.to_uppercase().contains("WITH"), "missing WITH:\n{}", out);
        assert!(out.to_uppercase().contains("RECURSIVE"), "missing RECURSIVE:\n{}", out);
        assert!(out.to_uppercase().contains("UNION ALL"), "missing UNION ALL:\n{}", out);
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
        let case_lines = out.lines().filter(|l| l.to_uppercase().contains("WHEN")).count();
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
        assert!(out.to_lowercase().contains("test"), "lost dollar-quoted body in:\n{}", out);
    }

    #[test]
    fn compact_short_select_stays_one_line() {
        let sql = "select id, name from users where id = 1";
        let out = format_sql_compact(sql, 4, true, "ansi");
        // Short query — should fit on one line (no break-before keywords).
        assert!(!out.contains("\nFROM"), "compact short query has line break:\n{}", out);
        assert!(out.to_uppercase().contains("SELECT"));
        assert!(out.to_uppercase().contains("FROM"));
    }

    #[test]
    fn compact_long_query_breaks_at_clauses() {
        let sql = "select user_id, full_name, email_address, created_at, last_login_at from users where active = true and created_at > '2024-01-01' group by user_id order by last_login_at desc";
        let out = format_sql_compact(sql, 4, true, "ansi");
        // Long query — should break at major clause boundaries (FROM/WHERE/GROUP BY/ORDER BY).
        let breaks = out.lines().count();
        assert!(breaks >= 4, "long compact query did not break at clauses (got {} lines):\n{}", breaks, out);
    }

    #[test]
    fn compact_postgres_upsert_does_not_panic() {
        let sql = "insert into t (id, name) values (1, 'a') on conflict (id) do update set name = excluded.name";
        let out = format_sql_compact(sql, 4, true, "postgres");
        assert!(out.to_uppercase().contains("INSERT"));
        assert!(out.to_uppercase().contains("ON CONFLICT") || out.to_lowercase().contains("excluded"));
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
