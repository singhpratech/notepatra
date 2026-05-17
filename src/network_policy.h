// SPDX-License-Identifier: GPL-3.0-or-later

// network_policy.h — by-construction allowlist for "private network" hosts.
//
// Used by the cloud-free build flavor (NOTEPATRA_NO_CLOUD).  The two
// callers — aipanel.cpp URL-paste validation and ollama.cpp final-gate
// before QNetworkAccessManager — both ask the same question:
//   "Is this URL safe to send under no-cloud policy?"
//
// "Private" here means anything not routable on the public internet:
// loopback, RFC1918 (10/8, 172.16/12, 192.168/16), CGNAT (100.64/10,
// covers Tailscale + VPN), link-local, IPv6 loopback (::1) and
// unique-local (fc00::/7), plus common corp-network DNS suffixes
// (*.local, *.lan, *.internal, *.corp, *.home, *.intranet).
//
// Name-based, not resolved-IP-based — we don't do DNS in the check.
// Hostnames that *could* resolve to a private IP (e.g.
// "myserver.example.com") are treated as PUBLIC.  Two reasons:
//   1. TOCTOU — DNS can flip between check and connect.
//   2. Admins can rebind their internal hosts to a *.local / *.lan
//      / *.corp suffix if they want the cloud-free build to accept
//      them.  Explicit > implicit.
//
// Without NOTEPATRA_NO_CLOUD the helper still compiles and works; it
// just isn't called from the gating sites.
#pragma once

#include <QString>
#include <QUrl>

namespace NotepatraNetworkPolicy {

bool isPrivateNetworkHost(const QUrl &url);
bool isPrivateNetworkHost(const QString &host);

}  // namespace NotepatraNetworkPolicy
