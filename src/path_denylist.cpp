// SPDX-License-Identifier: GPL-3.0-or-later

#include "path_denylist.h"

#include "config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringList>

namespace PathDenylist {

namespace {

QString normalized(const QString &path) {
    QString p = path.toLower();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (p.size() > 1 && p.endsWith(QLatin1Char('/'))) p.chop(1);
    return p;
}

// Each directory in its spelled and canonical (symlink-resolved) forms,
// normalized. Canonicalizing stats the disk, so the result is cached per
// set of spelled paths (they change only when HOME / XDG / APPDATA do).
QStringList withCanonicalForms(const QStringList &raw) {
    static QMutex mutex;
    static QHash<QString, QStringList> cache;
    const QString key = raw.join(QChar(0));
    QMutexLocker lock(&mutex);
    const auto hit = cache.constFind(key);
    if (hit != cache.constEnd()) return hit.value();
    if (cache.size() > 64) cache.clear();  // env churn (tests): stay bounded
    QStringList forms;
    for (const QString &d : raw) {
        if (d.isEmpty()) continue;
        forms << normalized(QDir::cleanPath(d));
        const QString canon = QFileInfo(d).canonicalFilePath();
        if (!canon.isEmpty()) forms << normalized(canon);
    }
    forms.removeDuplicates();
    cache.insert(key, forms);
    return forms;
}

bool underAny(const QString &p, const QStringList &dirs) {
    for (const QString &d : dirs)
        if (p == d || p.startsWith(d + QLatin1Char('/'))) return true;
    return false;
}

// The app's own settings: config.json (cloud API keys), db-connections.json
// (saved passwords), rest-client.json, the MCP endpoint file, session and
// recovery state, AI logs, chat and search history. Nothing there is a file
// the app offers the user to edit through a tool: Open Plugins Folder opens
// a file manager and Noter lives under Documents.
QStringList appSettingsDirs() {
    QStringList dirs = {Config::appConfigDirPath(), Config::legacyLinuxConfigDir()};
    // QStandardPaths' per-app locations are per-app only once an application
    // name exists; without one AppConfigLocation is the whole of ~/.config.
    if (!QCoreApplication::applicationName().isEmpty()) {
        const QString appCfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        if (appCfg != QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
            dirs << appCfg;
        // Plugins load from here at start-up: writable = code execution.
        const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!appData.isEmpty()) dirs << appData + QStringLiteral("/plugins");
    }
    return withCanonicalForms(dirs);
}

}  // namespace

bool isAppSettingsPath(const QString &absPath) {
    if (absPath.isEmpty()) return false;
    return underAny(normalized(QDir::cleanPath(QDir::fromNativeSeparators(absPath))),
                    appSettingsDirs());
}

bool isSecretPath(const QString &absPath) {
    if (isAppSettingsPath(absPath)) return true;
    const QString p = absPath.toLower();

    // Segment matches — credential directories and system password files.
    // Both forward- and back-slash variants, because canonical paths on
    // Windows sometimes preserve the separators the OS API handed back.
    // Matching on SEGMENTS (leading + trailing separator) keeps a project
    // folder legitimately named "ssh" or "aws" out of the deny-list.
    static const QStringList kSegments = {
        QStringLiteral("/.ssh/"),   QStringLiteral("\\.ssh\\"),
        QStringLiteral("/.gnupg/"), QStringLiteral("\\.gnupg\\"),
        QStringLiteral("/.aws/"),   QStringLiteral("\\.aws\\"),
        QStringLiteral("/.netrc"),  QStringLiteral("\\.netrc"),
        QStringLiteral("/.npmrc"),  QStringLiteral("/.pypirc"),
        QStringLiteral("/.docker/config.json"),
        QStringLiteral("/etc/passwd"), QStringLiteral("/etc/shadow"),
    };
    for (const QString &needle : kSegments)
        if (p.contains(needle)) return true;

    // Extension matches — private keys, keystores, and Terraform files.
    // *.pem / *.key are not ALWAYS credentials (test fixtures use them), but
    // the false-positive cost is one refused read and the false-negative cost
    // is a leaked key, so this errs toward refusing.
    //
    // .jks came from the git-hunk-apply copy of this list and was missing from
    // the AI-tools copy — the exact drift this file exists to end.
    static const QStringList kSuffixes = {
        QStringLiteral(".pem"),  QStringLiteral(".key"),
        QStringLiteral(".pfx"),  QStringLiteral(".p12"),
        QStringLiteral(".jks"),  QStringLiteral(".tfvars"),
        QStringLiteral(".tfstate"),
    };
    for (const QString &suffix : kSuffixes)
        if (p.endsWith(suffix)) return true;

    // Filename patterns, separator-anchored so "secretsjson" or "myenv" at
    // the workspace root does not false-positive.
    static const QStringList kFilenames = {
        QStringLiteral("id_rsa"),          QStringLiteral("id_ed25519"),
        QStringLiteral("id_ecdsa"),        QStringLiteral("id_dsa"),
        QStringLiteral("authorized_keys"), QStringLiteral("known_hosts"),
        QStringLiteral(".env"),            QStringLiteral("secrets.json"),
    };
    for (const QString &name : kFilenames)
        if (p.contains(QLatin1Char('/') + name) ||
            p.contains(QLatin1Char('\\') + name))
            return true;

    // Unanchored id_rsa, kept from the git-hunk-apply list: an SSH key copied
    // into a build directory as `backup-id_rsa` is still an SSH key. Dropping
    // it in favour of the anchored form above would have narrowed a guard that
    // already shipped, so the union keeps both.
    if (p.contains(QStringLiteral("id_rsa"))) return true;

    // The dotenv convention also covers <name>.env (app.env, prod.env) and
    // *.env.<suffix> (app.env.bak), which the anchored "/.env" match misses.
    if (p.endsWith(QStringLiteral(".env")) ||
        p.contains(QStringLiteral(".env.")))
        return true;

    return false;
}

bool isWriteDangerous(const QString &absPath) {
    QString p = absPath.toLower();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const int slash = p.lastIndexOf(QLatin1Char('/'));
    const QString name = slash >= 0 ? p.mid(slash + 1) : p;

    // Shell start-up files: run by every new terminal.
    static const QStringList kRcNames = {
        QStringLiteral(".bashrc"),   QStringLiteral(".bash_profile"),
        QStringLiteral(".bash_login"), QStringLiteral(".bash_logout"),
        QStringLiteral(".profile"),  QStringLiteral(".zshrc"),
        QStringLiteral(".zshenv"),   QStringLiteral(".zprofile"),
        QStringLiteral(".zlogin"),   QStringLiteral(".cshrc"),
        QStringLiteral(".tcshrc"),   QStringLiteral(".kshrc"),
        QStringLiteral(".xprofile"), QStringLiteral(".xsession"),
        QStringLiteral(".xinitrc"),  QStringLiteral(".gitconfig"),
        QStringLiteral("authorized_keys"), QStringLiteral("known_hosts"),
        QStringLiteral("microsoft.powershell_profile.ps1"),
        QStringLiteral("profile.ps1"),
    };
    if (kRcNames.contains(name)) return true;

    // A `.git` file redirects git to another directory (gitdir: ...), whose
    // config then runs code like the repository's own.
    if (name == QLatin1String(".git")) return true;

    // Directories whose contents execute on login, on a schedule, or on a
    // git operation. Everything under a .git directory counts: config
    // (core.fsmonitor / core.hooksPath / core.pager / aliases run on the app's
    // next `git status`), config.worktree, info/, hooks/, and the same files
    // of submodules (.git/modules/*/) and worktrees (.git/worktrees/*/).
    static const QStringList kDirs = {
        QStringLiteral("/.config/autostart/"),
        QStringLiteral("/.config/fish/"),
        QStringLiteral("/.config/systemd/user/"),
        QStringLiteral("/.config/environment.d/"),
        QStringLiteral("/.config/git/"),  // XDG git config / attributes / ignore
        QStringLiteral("/.bashrc.d/"),
        // A .desktop entry here overrides the system one of the same name:
        // the next launch of that app from the menu runs its Exec= line.
        QStringLiteral("/.local/share/applications/"),
        QStringLiteral("/library/launchagents/"),
        QStringLiteral("/library/launchdaemons/"),
        QStringLiteral("/start menu/programs/startup/"),
        QStringLiteral("/var/spool/cron/"),
        QStringLiteral("/etc/cron"),
        QStringLiteral("/etc/profile.d/"),
        QStringLiteral("/etc/gitconfig"),
        QStringLiteral("/.git/"),
        QStringLiteral("/.ssh/"),
    };
    for (const QString &d : kDirs)
        if (p.contains(d)) return true;

    // $XDG_CONFIG_HOME/git/ when XDG_CONFIG_HOME is not ~/.config.
    const QString xdg = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (!xdg.isEmpty() &&
        underAny(normalized(QDir::cleanPath(p)),
                 withCanonicalForms({xdg + QStringLiteral("/git")})))
        return true;

    return isAppSettingsPath(absPath);
}

} // namespace PathDenylist
