// SPDX-License-Identifier: GPL-3.0-or-later
//
// msvc_stdext_shim.h — Visual Studio 2026 (VS 18, MSVC toolset 14.5x,
// _MSC_VER >= 1950) REMOVED stdext::make_checked_array_iterator from <iterator>.
// Qt 5.15.2 still calls it from QtCore/qlist.h via the QT_MAKE_CHECKED_ARRAY_ITERATOR
// macro, so EVERY translation unit that instantiates a QList fails to compile:
//     error C2653: 'stdext': is not a class or namespace name
//     error C3861: 'make_checked_array_iterator': identifier not found
//
// GitHub's hosted windows-latest AND windows-2025 images both rolled to VS2026
// in mid-2026 and no VS2022 image remains, so this can't be fixed at the runner
// level — it needs a code-level stand-in. We provide a minimal replacement that
// returns the raw pointer, byte-identical in behaviour to the non-MSVC
// QT_MAKE_CHECKED_ARRAY_ITERATOR(x, N) => (x) branch Qt already ships on
// Linux/macOS. Bounds are intentionally ignored (same as that path). This header
// is force-included (/FI) before every Qt header on MSVC; see CMakeLists.txt.
//
// Guarded to _MSC_VER >= 1950 (VS2026+) so it can never collide with an older
// toolchain's real stdext::make_checked_array_iterator, which returned a
// stdext::checked_array_iterator<T*> (a different type) and is still present on
// VS2019 / earlier VS2022.
#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1950
#include <cstddef>
namespace stdext {
template <class T>
inline T *make_checked_array_iterator(T *ptr, std::size_t /*size*/,
                                      std::size_t offset = 0) {
    return ptr + offset;
}
}  // namespace stdext
#endif
