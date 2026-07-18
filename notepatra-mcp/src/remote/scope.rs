// SPDX-License-Identifier: GPL-3.0-or-later
//! Scope + tier model. The tier partition is derived ENTIRELY from
//! `tools::{READ_TOOLS, ACT_TOOLS, WRITE_TOOLS}` (already `pub`, already
//! partition-tested in tests/protocol.rs), so scope enforcement stays SSOT-
//! driven: a future untiered tool fails the existing partition test before it
//! could dodge the gate here.

use serde_json::Value;

use crate::tools::{ACT_TOOLS, READ_TOOLS, WRITE_TOOLS};

/// A tool's capability tier, ordered Read < Act < Write.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Tier {
    Read,
    Act,
    Write,
}

/// A connection's granted scope. Each scope admits its tier and every lower
/// one: read_only ⊂ read_act ⊂ write_request.
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Scope {
    ReadOnly,
    ReadAct,
    WriteRequest,
}

impl Scope {
    /// Wire token: `"read_only" | "read_act" | "write_request"`.
    pub fn as_str(self) -> &'static str {
        match self {
            Scope::ReadOnly => "read_only",
            Scope::ReadAct => "read_act",
            Scope::WriteRequest => "write_request",
        }
    }

    pub fn parse(s: &str) -> Option<Scope> {
        match s {
            "read_only" => Some(Scope::ReadOnly),
            "read_act" => Some(Scope::ReadAct),
            "write_request" => Some(Scope::WriteRequest),
            _ => None,
        }
    }

    /// Highest tier this scope may invoke.
    fn max_tier(self) -> Tier {
        match self {
            Scope::ReadOnly => Tier::Read,
            Scope::ReadAct => Tier::Act,
            Scope::WriteRequest => Tier::Write,
        }
    }

    /// Whether a tool of `tier` is allowed at this scope.
    pub fn allows(self, tier: Tier) -> bool {
        tier <= self.max_tier()
    }
}

/// The tier of `name`, or `None` if it is not a known tool (unknown tools are
/// forwarded and answered "Unknown tool" by the server — no elevation possible).
pub fn tier_of(name: &str) -> Option<Tier> {
    if READ_TOOLS.contains(&name) {
        Some(Tier::Read)
    } else if ACT_TOOLS.contains(&name) {
        Some(Tier::Act)
    } else if WRITE_TOOLS.contains(&name) {
        Some(Tier::Write)
    } else {
        None
    }
}

/// Filters a `tools/list` result in place, dropping every tool whose tier
/// exceeds `scope`. Enforcement at dispatch (the gateway rejecting an
/// over-scope `tools/call`) is the real gate; this is honesty in advertising.
pub fn filter_tools_list(scope: Scope, resp: &mut Value) {
    if let Some(tools) = resp
        .get_mut("result")
        .and_then(|r| r.get_mut("tools"))
        .and_then(Value::as_array_mut)
    {
        tools.retain(|t| {
            let name = t.get("name").and_then(Value::as_str).unwrap_or("");
            // An untiered name (shouldn't happen — partition-tested) is hidden
            // rather than advertised, matching the fail-closed dispatch rule.
            tier_of(name).map(|tier| scope.allows(tier)).unwrap_or(false)
        });
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tools::definitions;

    #[test]
    fn every_tool_is_tiered() {
        let defs = definitions();
        for t in defs.as_array().expect("tools array") {
            let name = t.get("name").and_then(Value::as_str).expect("name");
            assert!(tier_of(name).is_some(), "tool {name} has no tier");
        }
    }

    #[test]
    fn allow_matrix() {
        // read_only: read only.
        assert!(Scope::ReadOnly.allows(Tier::Read));
        assert!(!Scope::ReadOnly.allows(Tier::Act));
        assert!(!Scope::ReadOnly.allows(Tier::Write));
        // read_act: +act.
        assert!(Scope::ReadAct.allows(Tier::Read));
        assert!(Scope::ReadAct.allows(Tier::Act));
        assert!(!Scope::ReadAct.allows(Tier::Write));
        // write_request: everything.
        assert!(Scope::WriteRequest.allows(Tier::Read));
        assert!(Scope::WriteRequest.allows(Tier::Act));
        assert!(Scope::WriteRequest.allows(Tier::Write));
    }

    #[test]
    fn filter_sizes_are_cumulative() {
        let full = serde_json::json!({ "result": { "tools": definitions() } });
        let total = definitions().as_array().unwrap().len();
        assert_eq!(total, 48);

        for (scope, want) in [
            (Scope::ReadOnly, READ_TOOLS.len()),
            (Scope::ReadAct, READ_TOOLS.len() + ACT_TOOLS.len()),
            (Scope::WriteRequest, total),
        ] {
            let mut r = full.clone();
            filter_tools_list(scope, &mut r);
            let n = r["result"]["tools"].as_array().unwrap().len();
            assert_eq!(n, want, "scope {} filtered to {n}, want {want}", scope.as_str());
        }
        // Concretely: 24 / 37 / 48.
        assert_eq!(READ_TOOLS.len(), 24);
        assert_eq!(READ_TOOLS.len() + ACT_TOOLS.len(), 37);
    }

    #[test]
    fn scope_roundtrip() {
        for s in [Scope::ReadOnly, Scope::ReadAct, Scope::WriteRequest] {
            assert_eq!(Scope::parse(s.as_str()), Some(s));
        }
        assert_eq!(Scope::parse("bogus"), None);
    }
}
