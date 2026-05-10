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
// actual download / verify / install pipeline lands in v0.1.65; for
// v0.1.64 the answers are: (a) charts pack is "installed" iff the binary
// was built with NOTEPATRA_WITH_WEBENGINE=1, (b) pluginDir() returns the
// future on-disk root so callers can already point users at the right
// path in install dialogs.
//
// NOT a runtime QPluginLoader integration yet. The Qt plugin host is
// overkill for what we need (we don't load arbitrary shared libraries
// from user-writable paths — that would be a credential-theft surface).
// Instead, v0.1.65 will bundle "the charts pack" as a notepatra-built
// shared library + manifest that we verify by SHA-256 before activating.
// ═══════════════════════════════════════════════════════════════════════

#include <QString>

namespace NotepatraPlugins {

// Canonical pack identifiers. Kept as constants so typos surface at compile
// time instead of in run-time "plugin not found" stack traces.
constexpr const char *kChartsPack = "charts";  // Vega-Lite + QtWebEngine
constexpr const char *kPdfPack    = "pdf";     // Poppler-Qt5 for vision PDFs

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
//
// v0.1.64 behaviour:
//   - "charts" → true iff binary was compiled with NOTEPATRA_WITH_WEBENGINE
//   - "pdf"    → false (no PDF pack ships in v0.1.64)
//
// v0.1.65 will additionally check pluginDir() for a verified manifest.
bool isInstalled(const QString &name);

// Human-readable description of the pack — appears in the install prompt.
// Returns "" for unknown names.
QString packDescription(const QString &name);

// Approximate download size in bytes, for display in install prompts.
// Returns 0 for unknown names.
qint64 approximateDownloadSize(const QString &name);

// Where users go for manual-install instructions in v0.1.64 (since the
// auto-installer ships in v0.1.65). Returns a docs URL.
QString manualInstallDocUrl(const QString &name);

} // namespace NotepatraPlugins

#endif // NOTEPATRA_PLUGIN_LOADER_H
