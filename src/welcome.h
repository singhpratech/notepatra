#ifndef WELCOME_H
#define WELCOME_H

#include <QWidget>
#include <QString>

class QLabel;
class QScrollArea;
class QVBoxLayout;

/**
 * Welcome tab shown on first launch (or any launch with no files to
 * restore). It's the first impression most users get of Notepatra and
 * it tells them — in 30 seconds or less — what they can actually do
 * with the app.
 *
 * Content:
 *   • Hero block — "Welcome to Notepatra" + tagline + version
 *   • Quick actions — New File / Open File / Open Folder
 *   • Recent files list (clickable; reads from Config)
 *   • Feature showcase — 9 cards covering the inbuilt plugins (AI,
 *     Terminal, Compare, JSON/HTML/SQL, Git, REST, Brackets, Macros)
 *   • Keyboard shortcuts reference
 *   • Links to docs + the Ollama setup page
 *   • "Don't show again" checkbox that persists to Config
 *
 * The widget emits signals rather than calling MainWindow directly so
 * it can live alongside mainwindow.cpp without circular includes.
 */
class WelcomeWidget : public QWidget {
    Q_OBJECT
public:
    explicit WelcomeWidget(QWidget *parent = nullptr);

signals:
    void actionNewFile();
    void actionOpenFile();
    void actionOpenFolder();
    void actionOpenRecent(const QString &path);
    void actionOpenMenu(const QString &menuName);  // e.g. "AIAssistant", "Terminal"
    void actionDismissForever();

private:
    void buildHeroSection(QVBoxLayout *parent);
    void buildQuickActions(QVBoxLayout *parent);
    void buildRecentFiles(QVBoxLayout *parent);
    void buildFeatureCards(QVBoxLayout *parent);
    void buildShortcutsSection(QVBoxLayout *parent);
    void buildFooter(QVBoxLayout *parent);

    QWidget *makeFeatureCard(const QString &icon, const QString &title,
                             const QString &description, const QString &actionId,
                             const QString &accentColor);
    QWidget *makeShortcutRow(const QString &keys, const QString &action);
};

#endif // WELCOME_H
