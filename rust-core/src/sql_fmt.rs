//! SQL Formatter — pretty-prints SQL using sqlformat crate.

use sqlformat::{format, FormatOptions, Indent, QueryParams};

pub fn format_sql(input: &str, indent_width: usize, uppercase: bool) -> String {
    let options = FormatOptions {
        indent: Indent::Spaces(indent_width as u8),
        uppercase: Some(uppercase),
        lines_between_queries: 2,
        ignore_case_convert: None,
    };
    format(input, &QueryParams::None, &options)
}
