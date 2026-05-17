// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugin.h"
#include <QDir>
#include <QDirIterator>
#include <cstdlib>

PluginManager::PluginManager() {}

PluginManager::~PluginManager() {
    for (auto &p : m_plugins) {
        if (p.fnCleanup) p.fnCleanup();
        if (p.lib) { p.lib->unload(); delete p.lib; }
    }
}

void PluginManager::loadPlugins(const QString &pluginDir) {
    QDir dir(pluginDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

#ifdef Q_OS_WIN
    QDirIterator it(pluginDir, {"*.dll"}, QDir::Files);
#elif defined(Q_OS_MAC)
    QDirIterator it(pluginDir, {"*.dylib"}, QDir::Files);
#else
    QDirIterator it(pluginDir, {"*.so"}, QDir::Files);
#endif
    while (it.hasNext()) {
        QString path = it.next();
        auto *lib = new QLibrary(path);

        if (!lib->load()) {
            delete lib;
            continue;
        }

        PluginInfo info;
        info.path = path;
        info.lib = lib;

        info.fnName    = (PluginInfo::NameFunc)    lib->resolve("notepatra_plugin_name");
        info.fnVersion = (PluginInfo::VersionFunc) lib->resolve("notepatra_plugin_version");
        info.fnAuthor  = (PluginInfo::AuthorFunc)  lib->resolve("notepatra_plugin_author");
        info.fnInit    = (PluginInfo::InitFunc)    lib->resolve("notepatra_plugin_init");
        info.fnRun     = (PluginInfo::RunFunc)     lib->resolve("notepatra_plugin_run");
        info.fnCleanup = (PluginInfo::CleanupFunc) lib->resolve("notepatra_plugin_cleanup");

        // Must have at least name and run
        if (!info.fnName || !info.fnRun) {
            lib->unload();
            delete lib;
            continue;
        }

        info.name    = QString::fromUtf8(info.fnName());
        info.version = info.fnVersion ? QString::fromUtf8(info.fnVersion()) : "1.0";
        info.author  = info.fnAuthor  ? QString::fromUtf8(info.fnAuthor())  : "Unknown";

        if (info.fnInit) info.fnInit();

        m_plugins.append(info);
    }
}

QString PluginManager::runPlugin(int index, const QString &text) {
    if (index < 0 || index >= m_plugins.size()) return text;
    auto &p = m_plugins[index];
    if (!p.fnRun) return text;

    QByteArray utf8 = text.toUtf8();
    char *result = p.fnRun(utf8.constData(), utf8.size());

    if (result) {
        QString out = QString::fromUtf8(result);
        free(result);
        return out;
    }
    return text;
}
