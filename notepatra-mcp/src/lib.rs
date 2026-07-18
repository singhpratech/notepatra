// SPDX-License-Identifier: GPL-3.0-or-later
// The tools::definitions() json! array is large enough (48 tools) to exceed
// the default macro recursion limit.
#![recursion_limit = "512"]
pub mod prompts;
pub mod server;
pub mod tools;
pub mod transport;
