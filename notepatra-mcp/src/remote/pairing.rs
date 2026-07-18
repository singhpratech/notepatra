// SPDX-License-Identifier: GPL-3.0-or-later
//! One-shot pairing handshake. `serve` prints a one-time 8-digit code (short
//! TTL, single-use, attempt-limited). The client proves knowledge of the code
//! via HMAC-SHA256(code, server_nonce) — the code itself never crosses the
//! wire. On success `serve` issues a 256-bit token (stored SHA-256-only).
//!
//! MITM is out of scope this phase (loopback-only ⇒ no untrusted network path);
//! TLS + cert-pinning for LAN binding is Phase 3b.

use std::collections::HashMap;
use std::time::{Duration, Instant};

use hmac::{Hmac, Mac};
use sha2::Sha256;

use super::scope::Scope;
use super::{gen_pairing_code, random_bytes, random_hex, to_hex};

type HmacSha256 = Hmac<Sha256>;

/// Default pairing window: 120 s, 5 attempts, single successful use.
pub const DEFAULT_TTL: Duration = Duration::from_secs(120);
pub const DEFAULT_ATTEMPTS: u32 = 5;

/// Ceiling on outstanding `/pair/start` nonces. `/pair/start` is unauthenticated,
/// so a flood could otherwise grow `pending` without bound (memory DoS); we prune
/// TTL-expired entries and evict the oldest at capacity. A legitimate pairer only
/// ever needs one live nonce, so eviction cannot lock them out.
const MAX_PENDING: usize = 64;

/// A pairing failure (all forms collapse to a terse client message so a probe
/// learns nothing beyond "try again / restart serve").
#[derive(Debug, PartialEq, Eq)]
pub enum PairError {
    /// Consumed, expired, or attempts exhausted.
    Closed,
    /// Unknown/absent pair_id in `/pair/complete`.
    UnknownPairId,
    /// HMAC did not verify (wrong code).
    BadMac,
}

impl PairError {
    pub fn message(&self) -> &'static str {
        match self {
            PairError::Closed => "pairing closed; restart serve to pair again",
            PairError::UnknownPairId => "unknown pairing session; call /pair/start first",
            PairError::BadMac => "pairing failed (incorrect code)",
        }
    }
}

/// The nonce a `/pair/start` handed out, awaiting its `/pair/complete`.
struct Pending {
    nonce: [u8; 32],
    created: Instant,
}

/// Server-side pairing session. Single code; multiple `/pair/start` nonces may
/// be outstanding but the first correct `/pair/complete` consumes the session.
pub struct PairingState {
    code: String,
    created: Instant,
    ttl: Duration,
    attempts_left: u32,
    consumed: bool,
    max_scope: Scope,
    pending: HashMap<String, Pending>,
}

impl PairingState {
    /// Fresh session with a random code, printed by `serve`.
    pub fn new(max_scope: Scope, ttl: Duration, attempts: u32) -> Self {
        Self::with_code(gen_pairing_code(), max_scope, ttl, attempts)
    }

    /// Explicit-code constructor (tests, and any deterministic driver).
    pub fn with_code(code: String, max_scope: Scope, ttl: Duration, attempts: u32) -> Self {
        Self {
            code,
            created: Instant::now(),
            ttl,
            attempts_left: attempts,
            consumed: false,
            max_scope,
            pending: HashMap::new(),
        }
    }

    pub fn code(&self) -> &str {
        &self.code
    }

    /// The `Instant` the session was created (tests derive an "expired" now).
    pub fn created(&self) -> Instant {
        self.created
    }

    pub fn max_scope(&self) -> Scope {
        self.max_scope
    }

    fn open(&self, now: Instant) -> bool {
        !self.consumed
            && self.attempts_left > 0
            && now.saturating_duration_since(self.created) < self.ttl
    }

    /// `/pair/start`: issues a fresh nonce bound to a new pair_id. Fails if the
    /// session is closed. Returns `(pair_id, nonce_hex)`.
    pub fn start(&mut self, now: Instant) -> Result<(String, String), PairError> {
        if !self.open(now) {
            return Err(PairError::Closed);
        }
        // Bound `pending`: drop TTL-expired nonces, then evict the oldest until
        // there is room. Keeps an unauthenticated /pair/start flood from growing
        // the map without limit.
        self.pending
            .retain(|_, p| now.saturating_duration_since(p.created) < self.ttl);
        while self.pending.len() >= MAX_PENDING {
            let Some(oldest) = self
                .pending
                .iter()
                .min_by_key(|(_, p)| p.created)
                .map(|(k, _)| k.clone())
            else {
                break;
            };
            self.pending.remove(&oldest);
        }
        let pair_id = random_hex(8); // 8 bytes → 16 hex
        let mut nonce = [0u8; 32];
        random_bytes(&mut nonce);
        let nonce_hex = to_hex(&nonce);
        self.pending.insert(pair_id.clone(), Pending { nonce, created: now });
        Ok((pair_id, nonce_hex))
    }

    /// `/pair/complete`: verifies `mac_hex == HMAC(code, nonce)` in constant
    /// time. Success consumes the session and returns the granted scope
    /// (min of requested and `max_scope`). A wrong MAC decrements the attempt
    /// budget and closes the session when it hits zero.
    pub fn complete(
        &mut self,
        pair_id: &str,
        mac_hex: &str,
        requested: Scope,
        now: Instant,
    ) -> Result<Scope, PairError> {
        if !self.open(now) {
            return Err(PairError::Closed);
        }
        let Some(pending) = self.pending.get(pair_id) else {
            return Err(PairError::UnknownPairId);
        };
        let Some(mac_bytes) = hex_to_bytes(mac_hex) else {
            self.charge_attempt();
            return Err(PairError::BadMac);
        };

        let mut mac = HmacSha256::new_from_slice(self.code.as_bytes())
            .expect("HMAC accepts any key length");
        mac.update(&pending.nonce);
        if mac.verify_slice(&mac_bytes).is_ok() {
            // Single-use: consume the whole session, drop all pending nonces.
            self.consumed = true;
            self.pending.clear();
            Ok(requested.min(self.max_scope))
        } else {
            self.charge_attempt();
            Err(PairError::BadMac)
        }
    }

    fn charge_attempt(&mut self) {
        self.attempts_left = self.attempts_left.saturating_sub(1);
        if self.attempts_left == 0 {
            self.consumed = true;
            self.pending.clear();
        }
    }
}

/// Client helper: `HMAC-SHA256(code, nonce_bytes)` as hex, for `/pair/complete`.
pub fn client_mac(code: &str, nonce_hex: &str) -> Option<String> {
    let nonce = hex_to_bytes(nonce_hex)?;
    let mut mac = HmacSha256::new_from_slice(code.as_bytes()).ok()?;
    mac.update(&nonce);
    Some(to_hex(&mac.finalize().into_bytes()))
}

fn hex_to_bytes(s: &str) -> Option<Vec<u8>> {
    if s.len() % 2 != 0 {
        return None;
    }
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).ok())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    const CODE: &str = "48211937";

    fn fresh() -> PairingState {
        PairingState::with_code(CODE.into(), Scope::WriteRequest, DEFAULT_TTL, DEFAULT_ATTEMPTS)
    }

    // Drives a full correct handshake, returning the granted scope.
    fn pair_ok(st: &mut PairingState, requested: Scope) -> Result<Scope, PairError> {
        let now = Instant::now();
        let (pid, nonce) = st.start(now).unwrap();
        let mac = client_mac(CODE, &nonce).unwrap();
        st.complete(&pid, &mac, requested, now)
    }

    #[test]
    fn correct_code_issues_token_scope() {
        let mut st = fresh();
        assert_eq!(pair_ok(&mut st, Scope::WriteRequest), Ok(Scope::WriteRequest));
    }

    #[test]
    fn granted_scope_is_min_of_requested_and_max() {
        let mut st =
            PairingState::with_code(CODE.into(), Scope::ReadAct, DEFAULT_TTL, DEFAULT_ATTEMPTS);
        // Requesting write_request but capped at read_act → read_act.
        assert_eq!(pair_ok(&mut st, Scope::WriteRequest), Ok(Scope::ReadAct));
    }

    #[test]
    fn wrong_code_rejected_and_charges_attempt() {
        let mut st = fresh();
        let now = Instant::now();
        let (pid, nonce) = st.start(now).unwrap();
        let bad = client_mac("00000000", &nonce).unwrap();
        assert_eq!(st.complete(&pid, &bad, Scope::ReadOnly, now), Err(PairError::BadMac));
        // Correct code still works afterward (attempts remain).
        assert!(pair_ok(&mut st, Scope::ReadOnly).is_ok());
    }

    #[test]
    fn attempts_exhaust_then_closed() {
        let mut st = fresh();
        let now = Instant::now();
        for _ in 0..DEFAULT_ATTEMPTS {
            let (pid, nonce) = st.start(now).unwrap();
            let bad = client_mac("00000000", &nonce).unwrap();
            assert_eq!(st.complete(&pid, &bad, Scope::ReadOnly, now), Err(PairError::BadMac));
        }
        // Session is now closed: even the correct code is rejected.
        let now2 = Instant::now();
        assert_eq!(st.start(now2), Err(PairError::Closed));
    }

    #[test]
    fn single_use_second_complete_closed() {
        let mut st = fresh();
        assert!(pair_ok(&mut st, Scope::ReadOnly).is_ok());
        // Any further pairing is refused.
        let now = Instant::now();
        assert_eq!(st.start(now), Err(PairError::Closed));
    }

    #[test]
    fn expired_code_rejected() {
        let mut st =
            PairingState::with_code(CODE.into(), Scope::WriteRequest, Duration::from_secs(1), 5);
        // A `now` past the TTL closes the window without any real sleep.
        let expired = st.created() + Duration::from_secs(2);
        assert_eq!(st.start(expired), Err(PairError::Closed));
    }

    #[test]
    fn pending_map_is_bounded_under_start_flood() {
        let mut st = fresh();
        let now = Instant::now();
        // Far more starts than the cap: the map must never exceed MAX_PENDING.
        for _ in 0..(MAX_PENDING * 4) {
            st.start(now).unwrap();
            assert!(st.pending.len() <= MAX_PENDING);
        }
        // A correct handshake against the newest nonce still succeeds.
        let (pid, nonce) = st.start(now).unwrap();
        let mac = client_mac(CODE, &nonce).unwrap();
        assert!(st.complete(&pid, &mac, Scope::ReadOnly, now).is_ok());
    }

    #[test]
    fn unknown_pair_id_rejected() {
        let mut st = fresh();
        let now = Instant::now();
        assert_eq!(
            st.complete("deadbeef", "00", Scope::ReadOnly, now),
            Err(PairError::UnknownPairId)
        );
    }
}
