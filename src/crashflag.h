// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>

// Precomputes the flag path into static storage and mkpaths its parent.
// Handlers must not allocate — call once at startup, after org/app names are set.
void crashFlagInit(const QString &flagPath);

// Async-signal-safe flag write. No-op before crashFlagInit / on overlong paths.
void crashFlagWrite();
