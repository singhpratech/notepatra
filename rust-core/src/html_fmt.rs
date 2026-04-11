//! HTML Formatter — indent HTML tags properly.

pub fn format_html(input: &str, indent_size: usize) -> String {
    let mut result = String::with_capacity(input.len() * 2);
    let mut indent = 0usize;
    let mut i = 0;
    let chars: Vec<char> = input.chars().collect();
    let len = chars.len();

    // Self-closing / void tags that don't increase indent
    let void_tags = [
        "br", "hr", "img", "input", "meta", "link", "area", "base", "col", "embed", "param",
        "source", "track", "wbr",
    ];

    while i < len {
        if chars[i] == '<' {
            // Find end of tag
            let tag_start = i;
            let mut tag_end = i + 1;
            while tag_end < len && chars[tag_end] != '>' {
                tag_end += 1;
            }
            if tag_end < len {
                tag_end += 1; // include >
            }

            let tag: String = chars[tag_start..tag_end].iter().collect();
            let tag_lower = tag.to_lowercase();

            // Extract tag name
            let name_start = if tag_lower.starts_with("</") { 2 } else { 1 };
            let name: String = tag_lower[name_start..]
                .chars()
                .take_while(|c| c.is_alphanumeric())
                .collect();

            let is_closing = tag_lower.starts_with("</");
            let is_self_closing = tag.ends_with("/>") || void_tags.contains(&name.as_str());
            let is_comment = tag_lower.starts_with("<!--");
            let is_doctype = tag_lower.starts_with("<!doctype");

            if is_closing {
                indent = indent.saturating_sub(indent_size);
                result.push_str(&" ".repeat(indent));
                result.push_str(&tag);
                result.push('\n');
            } else if is_self_closing || is_comment || is_doctype {
                result.push_str(&" ".repeat(indent));
                result.push_str(&tag);
                result.push('\n');
            } else {
                result.push_str(&" ".repeat(indent));
                result.push_str(&tag);
                result.push('\n');
                indent += indent_size;
            }

            i = tag_end;
        } else if chars[i].is_whitespace() {
            i += 1;
        } else {
            // Text content
            let text_start = i;
            while i < len && chars[i] != '<' {
                i += 1;
            }
            let text: String = chars[text_start..i].iter().collect();
            let trimmed = text.trim();
            if !trimmed.is_empty() {
                result.push_str(&" ".repeat(indent));
                result.push_str(trimmed);
                result.push('\n');
            }
        }
    }

    result
}
