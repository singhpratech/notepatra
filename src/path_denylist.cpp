// SPDX-License-Identifier: GPL-3.0-or-later

#include "path_denylist.h"

#include <QStringList>

namespace PathDenylist {

bool isSecretPath(const QString &absPath) {
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

} // namespace PathDenylist
