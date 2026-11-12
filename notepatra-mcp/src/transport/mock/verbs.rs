// SPDX-License-Identifier: GPL-3.0-or-later
//! Mock answers for the v0.1.133 verbs that go through
//! [`EditorTransport::call_verb`](super::super::EditorTransport::call_verb).
//! Shapes mirror the C++ bridge (src/mcp_bridge.cpp) exactly.

use serde_json::Value;

use super::{MockEditor, TransportError};

impl MockEditor {
    pub(super) fn mock_verb(&mut self, verb: &str, _args: &Value) -> Result<Value, TransportError> {
        Err(TransportError(format!("unknown verb: {verb}")))
    }
}
