#ifndef CONFIG_H
#define CONFIG_H

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QDir>

/**
 * Persistent config — saves ALL settings to ~/.config/notepatra/config.json
 */
class Config {
public:
    static Config &instance() { static Config c; return c; }

    // Editor
    QString theme = "Dark";
    int tabWidth = 4;
    bool useTabs = false;
    bool autoIndent = true;
    bool wordWrap = false;
    bool showLineNumbers = true;
    bool showIndentGuides = true;
    int fontSize = 11;
    QString fontFamily;
    int edgeColumn = 120;
    bool showEdge = true;
    bool highlightCurrentLine = true;
    int caretWidth = 2;
    bool showDocumentRulers = false;
    bool showCrosshair = false;

    // Auto-complete
    bool autoComplete = true;
    int autoCompleteThreshold = 3;

    // Session
    QStringList recentFiles;
    int maxRecent = 15;

    // Window
    int windowX = -1, windowY = -1, windowW = 1280, windowH = 800;
    bool maximized = false;

    void load() {
        QString path = configPath();
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) return;
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

        theme = o.value("theme").toString(theme);
        tabWidth = o.value("tabWidth").toInt(tabWidth);
        useTabs = o.value("useTabs").toBool(useTabs);
        autoIndent = o.value("autoIndent").toBool(autoIndent);
        wordWrap = o.value("wordWrap").toBool(wordWrap);
        showLineNumbers = o.value("showLineNumbers").toBool(showLineNumbers);
        showIndentGuides = o.value("showIndentGuides").toBool(showIndentGuides);
        fontSize = o.value("fontSize").toInt(fontSize);
        fontFamily = o.value("fontFamily").toString(fontFamily);
        edgeColumn = o.value("edgeColumn").toInt(edgeColumn);
        showEdge = o.value("showEdge").toBool(showEdge);
        highlightCurrentLine = o.value("highlightCurrentLine").toBool(highlightCurrentLine);
        caretWidth = o.value("caretWidth").toInt(caretWidth);
        showDocumentRulers = o.value("showDocumentRulers").toBool(showDocumentRulers);
        showCrosshair = o.value("showCrosshair").toBool(showCrosshair);
        autoComplete = o.value("autoComplete").toBool(autoComplete);
        autoCompleteThreshold = o.value("autoCompleteThreshold").toInt(autoCompleteThreshold);
        windowX = o.value("windowX").toInt(windowX);
        windowY = o.value("windowY").toInt(windowY);
        windowW = o.value("windowW").toInt(windowW);
        windowH = o.value("windowH").toInt(windowH);
        maximized = o.value("maximized").toBool(maximized);

        recentFiles.clear();
        for (const auto &v : o.value("recentFiles").toArray())
            recentFiles.append(v.toString());
    }

    void save() {
        QDir().mkpath(QFileInfo(configPath()).path());
        QJsonObject o;
        o["theme"] = theme;
        o["tabWidth"] = tabWidth;
        o["useTabs"] = useTabs;
        o["autoIndent"] = autoIndent;
        o["wordWrap"] = wordWrap;
        o["showLineNumbers"] = showLineNumbers;
        o["showIndentGuides"] = showIndentGuides;
        o["fontSize"] = fontSize;
        o["fontFamily"] = fontFamily;
        o["edgeColumn"] = edgeColumn;
        o["showEdge"] = showEdge;
        o["highlightCurrentLine"] = highlightCurrentLine;
        o["caretWidth"] = caretWidth;
        o["showDocumentRulers"] = showDocumentRulers;
        o["showCrosshair"] = showCrosshair;
        o["autoComplete"] = autoComplete;
        o["autoCompleteThreshold"] = autoCompleteThreshold;
        o["windowX"] = windowX;
        o["windowY"] = windowY;
        o["windowW"] = windowW;
        o["windowH"] = windowH;
        o["maximized"] = maximized;

        QJsonArray arr;
        for (const auto &f : recentFiles) arr.append(f);
        o["recentFiles"] = arr;

        QFile f(configPath());
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson());
    }

    void addRecent(const QString &path) {
        recentFiles.removeAll(path);
        recentFiles.prepend(path);
        while (recentFiles.size() > maxRecent) recentFiles.removeLast();
    }

    static QString configPath() {
        return QDir::homePath() + "/.config/notepatra/config.json";
    }

private:
    Config() { load(); }
};

#endif
