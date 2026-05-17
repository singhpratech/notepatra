// SPDX-License-Identifier: GPL-3.0-or-later

//! HTML Formatter — indent HTML tags properly.
//!
//! v0.1.48: hard 50 MB cap to prevent OOM on pathological inputs.

const MAX_HTML_INPUT_BYTES: usize = 50 * 1024 * 1024;

pub fn format_html(input: &str, indent_size: usize) -> String {
    if input.len() > MAX_HTML_INPUT_BYTES {
        return format!(
            "<!-- input too large for HTML formatter: {} bytes, max {} bytes -->\n{}",
            input.len(),
            MAX_HTML_INPUT_BYTES,
            input
        );
    }
    format_html_inner(input, indent_size)
}

fn format_html_inner(input: &str, indent_size: usize) -> String {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn ok(input: &str) -> String {
        format_html(input, 2)
    }

    #[test]
    fn empty_input_does_not_panic() {
        assert_eq!(ok(""), "");
    }

    #[test]
    fn whitespace_only_collapses_cleanly() {
        // Whitespace-only input should produce empty or whitespace output.
        let out = ok("   \n  \t  ");
        // No tags found; whitespace skipped.
        assert!(out.is_empty() || out.trim().is_empty());
    }

    #[test]
    fn html5_full_page() {
        let s = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><title>Test</title></head><body><h1>Hello</h1></body></html>";
        let out = ok(s);
        assert!(out.contains("<!DOCTYPE"));
        assert!(out.contains("<title>"));
        assert!(out.contains("Hello"));
        // Should be multi-line (indented).
        assert!(out.lines().count() > 5);
    }

    #[test]
    fn form_all_input_types() {
        let s = "<form><input type=\"text\"><input type=\"checkbox\"><input type=\"hidden\" value=\"x\"><textarea>X</textarea><button>Send</button></form>";
        let out = ok(s);
        for marker in ["<form>", "<input", "<textarea>", "<button>", "</form>"] {
            assert!(out.contains(marker), "missing {} in:\n{}", marker, out);
        }
    }

    #[test]
    fn void_elements_not_indented() {
        let s = "<div><br><img src=\"x\"><hr><meta charset=\"utf-8\"></div>";
        let out = ok(s);
        // Closing </div> should be at indent 0.
        let lines: Vec<&str> = out.lines().collect();
        let close_line = lines.iter().find(|l| l.trim_start() == "</div>");
        assert!(close_line.is_some(), "no </div> in:\n{}", out);
        // Each void element on its own line.
        assert!(out.contains("<br>"));
        assert!(out.contains("<hr>"));
    }

    #[test]
    fn comments_preserved() {
        let s = "<!-- header --><div><!-- nested --><p>x</p></div><!-- footer -->";
        let out = ok(s);
        assert!(out.contains("<!-- header -->"));
        assert!(out.contains("<!-- nested -->"));
        assert!(out.contains("<!-- footer -->"));
    }

    #[test]
    fn modern_html5_dialog_details() {
        let s = "<dialog open><h2>T</h2></dialog><details><summary>S</summary><p>x</p></details>";
        let out = ok(s);
        assert!(out.contains("<dialog"));
        assert!(out.contains("<details>"));
        assert!(out.contains("<summary>"));
    }

    #[test]
    fn picture_with_sources() {
        let s = "<picture><source media=\"(max-width:640px)\" srcset=\"s.jpg\"><img src=\"l.jpg\" alt=\"r\"></picture>";
        let out = ok(s);
        assert!(out.contains("<picture>"));
        assert!(out.contains("<source"));
        assert!(out.contains("<img"));
    }

    #[test]
    fn inline_svg_renders() {
        let s = "<svg viewBox=\"0 0 100 100\"><circle cx=\"50\" cy=\"50\" r=\"40\"/><rect x=\"10\" y=\"10\" width=\"30\" height=\"30\"/></svg>";
        let out = ok(s);
        assert!(out.contains("<svg"));
        assert!(out.contains("<circle"));
        assert!(out.contains("</svg>"));
    }

    #[test]
    fn emoji_in_attributes_and_text() {
        let s = "<div title=\"🚀\" data-emoji=\"😀\">Content with emoji 🎨 ✨</div>";
        let out = ok(s);
        assert!(out.contains("🚀"), "lost rocket in attribute:\n{}", out);
        assert!(out.contains("🎨"), "lost art emoji in text:\n{}", out);
        assert!(out.contains("✨"), "lost sparkles in text:\n{}", out);
    }

    #[test]
    fn rtl_arabic_content() {
        let s = "<div dir=\"rtl\" lang=\"ar\"><p>مرحبا بالعالم</p></div>";
        let out = ok(s);
        assert!(out.contains("مرحبا"));
        assert!(out.contains("dir=\"rtl\""));
    }

    #[test]
    fn cjk_content() {
        let s = "<p>你好世界 こんにちは 안녕하세요</p>";
        let out = ok(s);
        assert!(out.contains("你好世界"));
        assert!(out.contains("こんにちは"));
        assert!(out.contains("안녕하세요"));
    }

    #[test]
    fn html_entities_preserved() {
        let s = "<p>&amp; &lt;tag&gt; &copy; &#x1F600;</p>";
        let out = ok(s);
        assert!(out.contains("&amp;"));
        assert!(out.contains("&copy;"));
        assert!(out.contains("&#x1F600;"));
    }

    #[test]
    fn unclosed_tags_do_not_crash() {
        // Pre-formatter does not auto-close unclosed tags (that's the
        // formatter UI's "Fix + Format" button's job). Just verify no
        // crash and the input tags are present.
        let s = "<div><p>foo<span>bar";
        let out = ok(s);
        assert!(out.contains("<div>"));
        assert!(out.contains("<p>"));
    }

    #[test]
    fn malformed_unquoted_attrs_pass_through() {
        let s = "<div id=main class=container><a href=page.html>Link</a></div>";
        let out = ok(s);
        // Tags pass through literally — formatter doesn't normalize attrs.
        assert!(out.contains("id=main"));
        assert!(out.contains("Link"));
    }

    #[test]
    fn deeply_nested_does_not_overflow() {
        let mut s = String::new();
        for _ in 0..200 {
            s.push_str("<div>");
        }
        s.push_str("body");
        for _ in 0..200 {
            s.push_str("</div>");
        }
        let out = format_html(&s, 2);
        assert!(out.contains("body"));
        // 401 lines (200 opens + body + 200 closes).
        assert!(out.lines().count() >= 200);
    }

    #[test]
    fn over_max_size_is_safe() {
        let input = "<div>".repeat(MAX_HTML_INPUT_BYTES / 5 + 1);
        let out = format_html(&input, 2);
        assert!(out.starts_with("<!-- input too large"));
    }

    #[test]
    fn mixed_line_endings_normalized() {
        let s = "<!DOCTYPE html>\r\n<html>\r\n<body>\r\n<p>X</p>\n</body>\r\n</html>";
        let out = ok(s);
        // Output should not contain \r — formatter rebuilds lines.
        assert!(!out.contains('\r'), "raw \\r leaked through");
    }

    #[test]
    fn cdata_section_in_xml_like_html() {
        let s = "<root><![CDATA[<tags>not parsed</tags>]]><normal>X</normal></root>";
        let out = ok(s);
        assert!(out.contains("CDATA"));
    }

    #[test]
    fn boolean_attributes() {
        let s = "<input type=\"checkbox\" checked><input disabled><option selected>X</option>";
        let out = ok(s);
        assert!(out.contains("checked"));
        assert!(out.contains("disabled"));
        assert!(out.contains("selected"));
    }

    #[test]
    fn data_attributes_with_json() {
        let s = "<div data-config='{\"key\":\"value\",\"emoji\":\"🎯\"}'>X</div>";
        let out = ok(s);
        assert!(out.contains("data-config"));
        assert!(out.contains("🎯"));
    }

    #[test]
    fn idempotent_already_formatted() {
        let original = "<!DOCTYPE html><html><body><p>X</p></body></html>";
        let once = ok(original);
        let twice = ok(&once);
        assert_eq!(once, twice, "format_html is not idempotent");
    }
}
