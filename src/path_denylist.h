// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

// Single source of truth for "this path holds credentials — never read it,
// never write it, never quote its contents back to a model".
//
// This list existed in TWO places and they had drifted apart, each covering
// holes the other left open: ai_tools.cpp knew about *.tfvars / *.tfstate /
// .pypirc and Windows backslash separators; git_hunk_apply.cpp knew about
// id_rsa and *.jks. Neither knew everything, and search_project consulted
// neither. What lives here is the UNION, checked by every caller.
//
// QtCore only; the one piece of state (canonical forms of the app's own
// directories) sits behind a mutex — safe to call from a QtConcurrent worker
// thread (search_project's filesystem leg does).
namespace PathDenylist {

// True when `absPath` looks like a secret store: an SSH/GPG/AWS/Docker
// credential directory, a private-key or Terraform state extension, a netrc /
// npmrc / pypirc, a system password file, or the app's own settings
// (isAppSettingsPath). Matched case-insensitively on path SEGMENTS so a
// project directory legitimately named "ssh" is not caught.
bool isSecretPath(const QString &absPath);

// True when writing `absPath` could plant code that runs later without the
// user opening it: shell rc/profile files, autostart / LaunchAgents / the
// Windows Startup folder, cron spools, anything under a .git directory (hooks,
// config, info, submodule and worktree config) or a `.git` file, the global
// and XDG git config, and the app's own settings. Reading most of these is
// harmless, so this list guards WRITES; callers that write must check
// isSecretPath too.
bool isWriteDangerous(const QString &absPath);

// True when `absPath` is inside the app's own settings: Config::appConfigDir()
// (config.json holds cloud API keys, db-connections.json saved passwords), the
// legacy ~/.config/notepatra copy, QStandardPaths' AppConfigLocation (chat and
// search history) and the plugins folder. Spelled and canonical forms both
// match; case-insensitive. The app's own reads and writes do not consult this.
bool isAppSettingsPath(const QString &absPath);

} // namespace PathDenylist
