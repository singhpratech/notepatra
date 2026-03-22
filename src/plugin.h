/**
 * Notepatra Plugin API
 *
 * To write a plugin:
 * 1. Create a shared library (.so) that exports these C functions
 * 2. Drop the .so file in ~/.config/notepatra/plugins/
 * 3. Restart Notepatra — it auto-loads all plugins
 *
 * Example plugin (save as myplugin.cpp, compile with):
 *   g++ -shared -fPIC -o myplugin.so myplugin.cpp $(pkg-config --cflags --libs Qt5Widgets)
 *
 * ─── Minimal plugin template ───
 *
 *   #include <cstring>
 *
 *   extern "C" {
 *       const char* notepatra_plugin_name()    { return "My Plugin"; }
 *       const char* notepatra_plugin_version() { return "1.0"; }
 *       const char* notepatra_plugin_author()  { return "Your Name"; }
 *
 *       // Called when plugin is loaded
 *       void notepatra_plugin_init() {}
 *
 *       // Called when user clicks the plugin menu item
 *       // text = current editor text, len = byte length
 *       // Return new text (malloc'd) or NULL to keep unchanged
 *       char* notepatra_plugin_run(const char* text, int len) {
 *           // Example: convert all text to uppercase
 *           char* result = (char*)malloc(len + 1);
 *           for (int i = 0; i < len; i++)
 *               result[i] = toupper(text[i]);
 *           result[len] = '\0';
 *           return result;
 *       }
 *
 *       // Called when plugin is unloaded
 *       void notepatra_plugin_cleanup() {}
 *   }
 */

#ifndef PLUGIN_H
#define PLUGIN_H

#include <QString>
#include <QVector>
#include <QLibrary>

struct PluginInfo {
    QString name;
    QString version;
    QString author;
    QString path;
    QLibrary *lib = nullptr;

    // Function pointers
    typedef const char* (*NameFunc)();
    typedef const char* (*VersionFunc)();
    typedef const char* (*AuthorFunc)();
    typedef void (*InitFunc)();
    typedef char* (*RunFunc)(const char*, int);
    typedef void (*CleanupFunc)();

    NameFunc    fnName = nullptr;
    VersionFunc fnVersion = nullptr;
    AuthorFunc  fnAuthor = nullptr;
    InitFunc    fnInit = nullptr;
    RunFunc     fnRun = nullptr;
    CleanupFunc fnCleanup = nullptr;
};

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    void loadPlugins(const QString &pluginDir);
    const QVector<PluginInfo> &plugins() const { return m_plugins; }
    QString runPlugin(int index, const QString &text);

private:
    QVector<PluginInfo> m_plugins;
};

#endif
