// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NOTEPATRA_PLUGIN_LOADER_H
#define NOTEPATRA_PLUGIN_LOADER_H

// ═══════════════════════════════════════════════════════════════════════
// v0.1.64 — Lite-mode plugin discovery.
//
// Bare-binary philosophy: ship a small core, let heavy features
// (QtWebEngine charts, Poppler PDF→PNG, future embedding models, future
// LSP servers) install on-demand the first time they're invoked. Mirrors
// DuckDB's extension model — small download, expand when needed.
//
// This scaffold gives the rest of the codebase a single place to ask
// "is the X pack installed?" and to look up where its files live. The
// charts pack is "installed" iff the binary was built with
// NOTEPATRA_WITH_WEBENGINE=1; pluginDir() returns the on-disk root so
// callers can point users at the right path in install dialogs.
//
// NOT a runtime QPluginLoader integration. The Qt plugin host is
// overkill for what we need (we don't load arbitrary shared libraries
// from user-writable paths — that would be a credential-theft surface).
// ═══════════════════════════════════════════════════════════════════════

#include <QString>

namespace NotepatraPlugins {

// Canonical pack identifiers. Kept as constants so typos surface at compile
// time instead of in run-time "plugin not found" stack traces.
constexpr const char *kChartsPack = "charts";  // Vega-Lite + QtWebEngine

// User-writable plugin root. Each pack lives at <pluginDir()>/<name>/.
// On Linux: ~/.local/share/notepatra/plugins/
// On macOS: ~/Library/Application Support/notepatra/plugins/
// On Windows: %APPDATA%/notepatra/plugins/
//
// Returns absolute path. Does NOT create the directory — that's an install
// step. Read-only callers can use this to display "where will it install?"
// strings.
QString pluginDir();

// Per-pack install path. Equivalent to pluginDir() + "/" + name. Same
// caveat: directory may not exist.
QString installPath(const QString &name);

// Returns true iff the pack named <name> is currently usable.
//   - "charts" → true iff binary was compiled with NOTEPATRA_WITH_WEBENGINE
bool isInstalled(const QString &name);

// Human-readable description of the pack — appears in the install prompt.
// Returns "" for unknown names.
QString packDescription(const QString &name);

// Approximate download size in bytes, for display in install prompts.
// Returns 0 for unknown names.
qint64 approximateDownloadSize(const QString &name);

// Where users go for manual-install instructions. Returns a docs URL.
QString manualInstallDocUrl(const QString &name);

} // namespace NotepatraPlugins

#endif // NOTEPATRA_PLUGIN_LOADER_H
