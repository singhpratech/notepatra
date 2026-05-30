// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin_loader.h"

#include <QDir>
#include <QStandardPaths>

namespace NotepatraPlugins {

QString pluginDir() {
    // QStandardPaths::AppDataLocation handles platform conventions:
    //   Linux:   ~/.local/share/notepatra
    //   macOS:   ~/Library/Application Support/notepatra
    //   Windows: %APPDATA%/notepatra
    // Falls back to the application's working dir if AppDataLocation is
    // empty (unusual; only happens in sandboxed test runners).
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        return QDir::current().absoluteFilePath(QStringLiteral("plugins"));
    }
    return QDir(base).absoluteFilePath(QStringLiteral("plugins"));
}

QString installPath(const QString &name) {
    return QDir(pluginDir()).absoluteFilePath(name);
}

bool isInstalled(const QString &name) {
    // v0.1.64 — charts pack is "installed" iff the binary was compiled
    // with WebEngine bundled. Future v0.1.65 will additionally check
    // installPath(name) for a SHA-256-verified manifest.
    if (name == QLatin1String(kChartsPack)) {
#ifdef NOTEPATRA_WITH_WEBENGINE
        return true;
#else
        return false;
#endif
    }
    return false;
}

QString packDescription(const QString &name) {
    if (name == QLatin1String(kChartsPack)) {
        return QStringLiteral(
            "Renders Vega-Lite charts (bar / line / scatter / area / "
            "composite) inline in the chat transcript. Powered by "
            "QtWebEngine + vega-embed — Linux & Windows Full builds only "
            "(macOS Full is DuckDB-only, no inline Vega rendering).");
    }
    if (name == QLatin1String(kPdfPack)) {
        return QStringLiteral(
            "Rasterises PDFs to PNG pages for vision-capable AI models. "
            "Powered by Poppler-Qt5.");
    }
    return QString();
}

qint64 approximateDownloadSize(const QString &name) {
    if (name == QLatin1String(kChartsPack)) {
        // There is no separately-downloadable charts pack — inline Vega
        // rendering ships in the Full binary (Linux & Windows; macOS Full
        // is DuckDB-only and has no QtWebEngine). Return 0 so the lite-stub
        // card omits any "download size" line rather than inventing one.
        return 0;
    }
    if (name == QLatin1String(kPdfPack)) {
        return 12LL * 1024LL * 1024LL;
    }
    return 0;
}

QString manualInstallDocUrl(const QString &name) {
    if (name == QLatin1String(kChartsPack)) {
        return QStringLiteral(
            "https://singhpratech.github.io/notepatra/docs.html#charts-pack");
    }
    if (name == QLatin1String(kPdfPack)) {
        return QStringLiteral(
            "https://singhpratech.github.io/notepatra/docs.html#pdf-pack");
    }
    return QStringLiteral("https://singhpratech.github.io/notepatra/docs.html");
}

} // namespace NotepatraPlugins
