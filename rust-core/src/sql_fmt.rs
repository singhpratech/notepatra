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
    let dialect = pick_dialect(dialect_name);
    let indent_width = indent_width.clamp(1, 8);

    match Parser::parse_sql(&*dialect, input) {
        Ok(stmts) if !stmts.is_empty() => {
            let mut out = String::new();
            for (i, stmt) in stmts.iter().enumerate() {
                if i > 0 {
                    out.push_str("\n\n");
                }
                let mut w = Writer::new(indent_width, uppercase);
                w.write_statement(stmt);
                out.push_str(&w.finish());
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
                lines_between_queries: 2,
                ignore_case_convert: None,
            };
            let legacy = legacy_format(input, &QueryParams::None, &options);
            format!(
                "-- (parser fallback: syntax unsupported by our parser)\n{}",
                legacy
            )
        }
    }
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
}

impl Writer {
    fn new(indent_width: usize, uppercase: bool) -> Self {
        Self {
            buf: String::new(),
            indent_width,
            uppercase,
            level: 0,
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
            other => {
                // INSERT / UPDATE / DELETE / DDL — fall back to sqlparser's
                // own Display. Not as pretty as the Query path but correct.
                let s = format!("{}", other);
                self.push_line(&s);
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
        let select_kw = if sel.distinct.is_some() {
            format!("{} {}", self.kw("SELECT"), self.kw("DISTINCT"))
        } else {
            self.kw("SELECT")
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
            self.push_line(&format!("{} {}", self.kw("GROUP BY"), gb_items.join(", ")));
        }

        if let Some(h) = &sel.having {
            self.push("\n");
            self.push_line(&format!("{} {}", self.kw("HAVING"), fmt_expr(h)));
        }
    }

    fn fmt_select_item(&self, item: &SelectItem) -> String {
        match item {
            SelectItem::UnnamedExpr(e) => fmt_expr(e),
            SelectItem::ExprWithAlias { expr, alias } => {
                format!("{} {} {}", fmt_expr(expr), self.kw("AS"), alias.value)
            }
            SelectItem::QualifiedWildcard(obj, _) => format!("{}.*", fmt_object_name(obj)),
            SelectItem::Wildcard(_) => "*".to_string(),
        }
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
    // subsequent get their AND/OR at column 4.
    fn write_where(&mut self, e: &Expr) {
        let parts = split_boolean(e);
        for (i, (op, part)) in parts.iter().enumerate() {
            self.push("\n");
            self.push(&self.sub_indent(1));
            if i == 0 {
                self.push(&fmt_expr(part));
            } else {
                self.push(&format!("{} {}", self.kw(op), fmt_expr(part)));
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
