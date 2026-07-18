// SPDX-License-Identifier: GPL-3.0-or-later
// The tools::definitions() json! array is large enough (48 tools) to exceed
// the default macro recursion limit.
#![recursion_limit = "512"]
pub mod prompts;
// Phase 3a — the opt-in remote gateway. Compiled ONLY under `--features remote`;
// the default build never touches it and pulls in zero new crates. This single
// line is the whole feature choke point.
#[cfg(feature = "remote")]
pub mod remote;
pub mod server;
pub mod tools;
pub mod transport;
