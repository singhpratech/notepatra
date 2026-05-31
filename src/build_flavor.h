#pragma once
// ─────────────────────────────────────────────────────────────────────
// Single source of truth for the human-facing build-flavor name.
//
// Only the LITE edition carries an edition suffix — it's the marker that
// tells a user "this bare build has no DuckDB engine". The Full edition
// (DuckDB bundled) self-identifies as the plain product name with NO "Full"
// suffix: "Notepatra" is the full-featured default, so it needs no qualifier.
// (v0.1.109: dropped the "Full" suffix; "Lite" stays for lite identification.)
//
// Two independent build axes:
//   • Edition: Full = the DuckDB engine is bundled (every platform, incl.
//     macOS Full which has DuckDB but no QtWebEngine) → no suffix. Lite =
//     bare → " Lite". Detected via NOTEPATRA_HAVE_DUCKDB (set in CMakeLists
//     when libduckdb is found); WITH_WEBENGINE is OR'd in as a
//     belt-and-suspenders marker since a WebEngine build always implies
//     DuckDB on Linux/Windows.
//   • Cloud: "Local AI" = the cloud-free build (NOTEPATRA_NO_CLOUD) that
//     blocks public-cloud LLM endpoints; the regular build omits it.
//
//   regular   Lite → "Notepatra Lite"
//   regular   Full → "Notepatra"
//   cloud-free Lite → "Notepatra Local AI Lite"
//   cloud-free Full → "Notepatra Local AI"
//
// NOTEPATRA_FLAVOR_NAME is a compile-time C string literal (via literal
// concatenation) so it works in both printf() and QString contexts.
// Keep app.setApplicationName("Notepatra") unchanged — that drives
// QSettings / config paths and must stay the bare product name.
// ─────────────────────────────────────────────────────────────────────

// Lite builds carry a " Lite" suffix; Full builds carry none.
#if defined(NOTEPATRA_HAVE_DUCKDB) || defined(NOTEPATRA_WITH_WEBENGINE)
#  define NOTEPATRA_EDITION_SUFFIX ""
#else
#  define NOTEPATRA_EDITION_SUFFIX " Lite"
#endif

#if defined(NOTEPATRA_NO_CLOUD)
#  define NOTEPATRA_FLAVOR_NAME "Notepatra Local AI" NOTEPATRA_EDITION_SUFFIX
#else
#  define NOTEPATRA_FLAVOR_NAME "Notepatra" NOTEPATRA_EDITION_SUFFIX
#endif
