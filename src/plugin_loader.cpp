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
            "QtWebEngine + vega-embed.");
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
        // QtWebEngine + dependencies. macOS/Win bundles QWE.framework
        // (~80 MB compressed); Linux uses the system libqt5webengine5
        // shared library (~30 MB on the wire, ~95 MB unpacked).
        return 95LL * 1024LL * 1024LL;
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
