#pragma once
// ─────────────────────────────────────────────────────────────────────
// Single source of truth for the human-facing build-flavor name.
//
// Two independent build axes produce four names:
//   • Edition: Full = the DuckDB engine is bundled (every platform,
//     incl. macOS Full which has DuckDB but no QtWebEngine). Lite = bare.
//     Detected via NOTEPATRA_HAVE_DUCKDB (set in CMakeLists when libduckdb
//     is found); WITH_WEBENGINE is OR'd in as a belt-and-suspenders marker
//     since a WebEngine build always implies DuckDB on Linux/Windows.
//   • Cloud: "Local AI" = the cloud-free build (NOTEPATRA_NO_CLOUD) that
//     blocks public-cloud LLM endpoints; the regular build omits it.
//
//   regular   Lite → "Notepatra Lite"
//   regular   Full → "Notepatra Full"
//   cloud-free Lite → "Notepatra Local AI Lite"
//   cloud-free Full → "Notepatra Local AI Full"
//
// NOTEPATRA_FLAVOR_NAME is a compile-time C string literal (via literal
// concatenation) so it works in both printf() and QString contexts.
// Keep app.setApplicationName("Notepatra") unchanged — that drives
// QSettings / config paths and must stay the bare product name.
// ─────────────────────────────────────────────────────────────────────

#if defined(NOTEPATRA_HAVE_DUCKDB) || defined(NOTEPATRA_WITH_WEBENGINE)
#  define NOTEPATRA_EDITION "Full"
#else
#  define NOTEPATRA_EDITION "Lite"
#endif

#if defined(NOTEPATRA_NO_CLOUD)
#  define NOTEPATRA_FLAVOR_NAME "Notepatra Local AI " NOTEPATRA_EDITION
#else
#  define NOTEPATRA_FLAVOR_NAME "Notepatra " NOTEPATRA_EDITION
#endif
