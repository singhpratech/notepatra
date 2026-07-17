// SPDX-License-Identifier: GPL-3.0-or-later
//! MCP prompts (spec 2025-06-18): three user-facing prompt templates that
//! embed live editor state. `listChanged` is not advertised — the list is
//! static.

use serde_json::{json, Value};

use crate::transport::{EditorTransport, TabSelector, TransportError};

/// How many notes "summarize-notes" embeds at most.
const MAX_NOTES_EMBEDDED: usize = 3;

pub enum GetError {
    /// Unknown prompt name — maps to JSON-RPC -32602 per spec.
    Unknown(String),
    /// The editor could not supply the data — maps to -32603.
    Editor(String),
}

impl From<TransportError> for GetError {
    fn from(e: TransportError) -> Self {
        GetError::Editor(e.0)
    }
}

pub fn definitions() -> Value {
    json!([
        {
            "name": "review-current-file",
            "description": "Code-review the file open in the active Notepatra tab.",
            "arguments": []
        },
        {
            "name": "explain-selection",
            "description": "Explain the text currently selected in Notepatra.",
            "arguments": []
        },
        {
            "name": "summarize-notes",
            "description": "Summarize the user's Noter notes (up to the 3 most recent).",
            "arguments": []
        }
    ])
}

pub fn get(transport: &mut dyn EditorTransport, name: &str) -> Result<Value, GetError> {
    let (description, text) = match name {
        "review-current-file" => review_current_file(transport)?,
        "explain-selection" => explain_selection(transport)?,
        "summarize-notes" => summarize_notes(transport)?,
        other => return Err(GetError::Unknown(other.to_string())),
    };
    Ok(json!({
        "description": description,
        "messages": [
            { "role": "user", "content": { "type": "text", "text": text } }
        ]
    }))
}

fn review_current_file(transport: &mut dyn EditorTransport) -> Result<(String, String), GetError> {
    let status = transport.get_status()?;
    // The bridge reports tab_index -1 when no tab is active.
    let tab_index = status
        .get("tab_index")
        .and_then(Value::as_i64)
        .ok_or_else(|| GetError::Editor("malformed get_status (missing tab_index)".into()))?;
    if tab_index < 0 {
        return Err(GetError::Editor("no active tab in the editor".into()));
    }
    let content = transport.read_tab(TabSelector::Index(tab_index as usize))?;
    let language = status
        .get("language")
        .and_then(Value::as_str)
        .unwrap_or("Plain Text");
    let text = format!(
        "Please review the file currently open in Notepatra.\n\
         Title: {title}\nLanguage: {language}\n\n\
         Point out bugs, risky patterns, and concrete improvements.\n\n\
         ```\n{body}```",
        title = content.title,
        body = content.text,
    );
    Ok(("Review the file in the active Notepatra tab".into(), text))
}

fn explain_selection(transport: &mut dyn EditorTransport) -> Result<(String, String), GetError> {
    let sel = transport.get_selection()?;
    let text = format!(
        "Explain the following text, currently selected in Notepatra. \
         Describe what it does and anything non-obvious about it.\n\n\
         ```\n{selection}\n```",
        selection = sel.text,
    );
    Ok(("Explain the currently selected text".into(), text))
}

fn summarize_notes(transport: &mut dyn EditorTransport) -> Result<(String, String), GetError> {
    let listing = transport.list_notes()?;
    let notes = listing
        .get("notes")
        .and_then(Value::as_array)
        .ok_or_else(|| GetError::Editor("malformed list_notes (missing notes)".into()))?;
    if notes.is_empty() {
        return Ok((
            "Summarize the user's Noter notes".into(),
            "The user has no Noter notes yet. Tell them so, and suggest creating one.".into(),
        ));
    }
    let mut text = String::from(
        "Summarize the following Noter notes from Notepatra. Give a short \
         overview per note, then any action items across all of them.\n",
    );
    for note in notes.iter().take(MAX_NOTES_EMBEDDED) {
        let file = note
            .get("file")
            .and_then(Value::as_str)
            .ok_or_else(|| GetError::Editor("malformed list_notes entry (missing file)".into()))?;
        let body = transport.read_note(file)?;
        let title = body.get("title").and_then(Value::as_str).unwrap_or(file);
        let note_text = body.get("text").and_then(Value::as_str).unwrap_or("");
        text.push_str(&format!("\n## {title}\n\n{note_text}\n"));
    }
    if notes.len() > MAX_NOTES_EMBEDDED {
        text.push_str(&format!(
            "\n({} more notes not shown.)\n",
            notes.len() - MAX_NOTES_EMBEDDED
        ));
    }
    Ok(("Summarize the user's Noter notes".into(), text))
}
