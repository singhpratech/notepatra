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
// QtCore only, no state, no allocation beyond the compare — safe to call from
// a QtConcurrent worker thread (search_project's filesystem leg does).
namespace PathDenylist {

// True when `absPath` looks like a secret store: an SSH/GPG/AWS/Docker
// credential directory, a private-key or Terraform state extension, a netrc /
// npmrc / pypirc, or a system password file. Matched case-insensitively on
// path SEGMENTS so a project directory legitimately named "ssh" is not caught.
bool isSecretPath(const QString &absPath);

} // namespace PathDenylist
