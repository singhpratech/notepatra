cmake_minimum_required(VERSION 3.16)
project(Notepatra VERSION 0.1.66 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
option(BUILD_TESTING "Build Notepatra regression tests" OFF)
option(BUILD_LEXER_TEST "Backward-compatible alias for BUILD_TESTING" OFF)
# v0.1.64 — Lite mode by default. The bare binary stays ~9 MB and the
# Vega-Lite renderer shows a "Charts Pack required" prompt with manual-
# install docs (auto-installer ships v0.1.65). Pass
# -DNOTEPATRA_WITH_WEBENGINE=ON at configure time for a "full" build that
# bundles QtWebEngine and renders charts inline.
#
# This mirrors DuckDB's extension philosophy: tiny core, optional heavy
# bits installed on demand. Keeps download / launch / disk footprint
# small for users who don't need charts.
option(NOTEPATRA_WITH_WEBENGINE "Bundle QtWebEngine for inline Vega-Lite chart rendering (full-mode build)" OFF)

if(BUILD_TESTING OR BUILD_LEXER_TEST)
    enable_testing()
    set(NOTEPATRA_BUILD_TESTS ON)
else()
    set(NOTEPATRA_BUILD_TESTS OFF)
endif()

# Use the dynamic CRT (/MD) on Windows MSVC, matching Qt5 from install-qt-action.
# Mixing /MT (static CRT) with /MD-built Qt produces CRT-heap mismatches that
# crash at runtime — see https://doc.qt.io/qt-5/windows-deployment.html.
# vcruntime140.dll / msvcp140.dll are preinstalled on Windows Server 2022
# runners and on every Windows install with the VC++ Redistributable
# (which most Windows machines already have).
if(WIN32 AND POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# v0.1.56: Force MSVC to interpret string literals as UTF-8.
# Without this, MSVC reads non-ASCII bytes (e.g. the 🔒 emoji at U+1F512) as
# the system code page (Windows-1252 on en-US), so QStringLiteral("🔒 Share")
# becomes mojibake at compile time. Affects every emoji / accented / non-Latin
# character in the codebase. The `/utf-8` flag sets both -execution-charset
# and -source-charset to UTF-8 — the canonical fix.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ── Platform detection ──
if(WIN32)
    set(PLATFORM_NAME "windows")
    set(EXE_NAME "notepatra")
elseif(APPLE)
    set(PLATFORM_NAME "macos")
    set(EXE_NAME "Notepatra")
else()
    set(PLATFORM_NAME "linux")
    set(EXE_NAME "notepatra")
endif()

message(STATUS "Building for platform: ${PLATFORM_NAME}")

# ── Qt5 ──
# v0.1.43: Sql + Charts added for Data Analyst mode.
# - Sql: SQLite driver ships with Qt by default; PostgreSQL/MySQL/SQL Server
#   require separate Qt SQL plugin packages users install themselves
#   (libqt5sql5-psql, libqt5sql5-mysql, libqt5sql5-odbc on Debian/Ubuntu;
#   `brew install qt@5` ships them all on macOS).
# - Charts: rendered inline in the AI chat transcript when the model emits
#   a ```chart fenced spec.
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Concurrent Sql Charts)

# v0.1.63 — optional QtWebEngine for Vega-Lite chart rendering. On distros
# that don't ship libqt5webengine5 (e.g. minimal CI containers), the option
# can be flipped off and VegaChartRenderer compiles a QLabel stub instead.
# We probe via find_package(... QUIET) so a missing component downgrades
# NOTEPATRA_WITH_WEBENGINE to OFF with a warning rather than failing the
# configure outright.
if(NOTEPATRA_WITH_WEBENGINE)
    find_package(Qt5 QUIET COMPONENTS WebEngineWidgets)
    if(Qt5WebEngineWidgets_FOUND)
        message(STATUS "Vega-Lite renderer: Qt5WebEngineWidgets ${Qt5WebEngineWidgets_VERSION}")
    else()
        message(WARNING
            "Qt5WebEngineWidgets not found — Vega-Lite renderer will compile "
            "a stub. Install libqt5webengine5-dev (Debian/Ubuntu) / "
            "qt5-webengine (Arch) / qt@5 includes it on macOS, then "
            "reconfigure. To silence this warning configure with "
            "-DNOTEPATRA_WITH_WEBENGINE=OFF.")
        set(NOTEPATRA_WITH_WEBENGINE OFF)
    endif()
endif()

# ── QScintilla ──
# Platform-specific search for QScintilla headers and library.
# On CI, these are typically passed via -DQSCINTILLA_INCLUDE=... -DQSCINTILLA_LIB=...
if(WIN32)
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATH_SUFFIXES Qsci)
    find_library(QSCINTILLA_LIB qscintilla2_qt5)
elseif(APPLE)
    # When QScintilla is built from source with Qt5's qmake, it installs into
    # the Qt5 prefix (e.g. /usr/local/opt/qt@5 or /opt/homebrew/opt/qt@5).
    # Also check Homebrew's own qscintilla2 prefix and common locations.
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATHS /usr/local/include /opt/homebrew/include
              /usr/local/opt/qscintilla2/include /opt/homebrew/opt/qscintilla2/include
              /usr/local/opt/qt@5/include /opt/homebrew/opt/qt@5/include
        PATH_SUFFIXES "" Qsci qt5/Qsci
        NO_DEFAULT_PATH)
    if(NOT QSCINTILLA_INCLUDE)
        find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h)
    endif()
    find_library(QSCINTILLA_LIB qscintilla2_qt5
        PATHS /usr/local/lib /opt/homebrew/lib
              /usr/local/opt/qscintilla2/lib /opt/homebrew/opt/qscintilla2/lib
              /usr/local/opt/qt@5/lib /opt/homebrew/opt/qt@5/lib
        NO_DEFAULT_PATH)
    if(NOT QSCINTILLA_LIB)
        find_library(QSCINTILLA_LIB qscintilla2_qt5)
    endif()
else()
    set(QSCINTILLA_INCLUDE_HINTS
        /usr/include
        /usr/include/qt5
        /usr/lib/qt5/include
    )
    if(CMAKE_LIBRARY_ARCHITECTURE)
        list(APPEND QSCINTILLA_INCLUDE_HINTS
            /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}
            /usr/include/${CMAKE_LIBRARY_ARCHITECTURE}/qt5
            /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/qt5/include
        )
    endif()
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATHS ${QSCINTILLA_INCLUDE_HINTS}
        PATH_SUFFIXES "" Qsci qt5/Qsci)
    if(NOT QSCINTILLA_INCLUDE)
        find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h)
    endif()
    find_library(QSCINTILLA_LIB qscintilla2_qt5)
endif()

# Validate that QScintilla was found
if(NOT QSCINTILLA_LIB)
    message(FATAL_ERROR "QScintilla library (qscintilla2_qt5) not found. "
        "Set -DQSCINTILLA_LIB=/path/to/libqscintilla2_qt5.dylib")
endif()
if(NOT QSCINTILLA_INCLUDE)
    message(FATAL_ERROR "QScintilla headers not found. "
        "Set -DQSCINTILLA_INCLUDE=/path/to/include (containing Qsci/ directory)")
endif()
message(STATUS "QScintilla lib: ${QSCINTILLA_LIB}")
message(STATUS "QScintilla include: ${QSCINTILLA_INCLUDE}")

# ── Rust core library ──
set(RUST_CORE_DIR ${CMAKE_SOURCE_DIR}/rust-core)
if(WIN32)
    set(RUST_LIB ${RUST_CORE_DIR}/target/release/notepad_core.lib)
else()
    set(RUST_LIB ${RUST_CORE_DIR}/target/release/libnotepad_core.a)
endif()

# Build Rust library as a custom command.
# Use BYPRODUCTS (not OUTPUT) so multi-config generators (Visual Studio) handle
# the externally-produced .lib correctly at link time.
add_custom_target(rust_core
    COMMAND cargo build --release
    BYPRODUCTS ${RUST_LIB}
    WORKING_DIRECTORY ${RUST_CORE_DIR}
    COMMENT "Building Rust core library..."
)

# ── Sources ──
# Auto-discover every src/*.cpp file. CONFIGURE_DEPENDS makes CMake re-run
# whenever a new .cpp lands in src/, so adding a feature just means dropping
# the file in — no manual SOURCES edit required. Eliminates the "I added a
# new src file but forgot to register it" class of release-day failure.
file(GLOB SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")

# v0.1.63 — pull in subsystem .cpp files (e.g. src/charts/*.cpp for the
# Vega-Lite renderer). Kept as a separate glob so reviewers see the
# subdirectory structure at a glance.
file(GLOB CHART_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/charts/*.cpp")
list(APPEND SOURCES ${CHART_SOURCES})

set(HEADERS
    src/mainwindow.h
    src/editor.h
    src/lexerutils.h
    src/lexer_go.h
    src/lexer_kotlin.h
    src/lexer_powershell.h
    src/lexer_rust.h
    src/lexer_swift.h
    src/lexer_typescript.h
    src/npp_palette.h
    src/tabmanager.h
    src/statusbar.h
    src/findreplace.h
    src/fileexplorer.h
    src/functionlist.h
    src/preferences.h
    src/rustbridge.h
    src/compare.h
    src/plugin.h
    src/terminal.h
    src/markdownpreview.h
    src/hexeditor.h
    src/gitgutter.h
    src/ollama.h
    src/aipanel.h
    src/inlineedit.h
    src/dbconnections.h
    src/csvanalyst.h
    src/chartrender.h
    src/restclient.h
    src/gitpanel.h
    src/sqlfmtpanel.h
    src/fmtpanel.h
    src/searchresults.h
    src/welcome.h
    src/projectsearch.h
    src/updater.h
    src/fonts.h
    src/ollamastatus.h
    src/themes.h
    src/config.h
    # v0.2 Coding-mode revamp (Slice B+C)
    src/diff_view.h
    src/edit_plan.h
    # v0.1.62 — per-hunk staging
    src/git_hunk_apply.h
    src/gutter_hunk_popup.h
    # v0.1.63 — Vega-Lite chart renderer (Data Analyst slice).
    src/charts/vega_chart_renderer.h
    # v0.1.64 — Lite-mode plugin discovery.
    src/plugin_loader.h
)

# ── Platform-specific resources ──
if(WIN32)
    # Windows: embed icon via .rc resource file
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/notepatra.rc")
        list(APPEND SOURCES resources/notepatra.rc)
    endif()
endif()

# ── Executable ──
if(APPLE)
    # macOS: build as an app bundle.
    #
    # We use a hand-written Info.plist template at resources/Info.plist.in
    # rather than letting CMake auto-generate a minimal one, because Tahoe
    # (macOS 26) silently refuses to launch ad-hoc-signed apps that lack
    # LSMinimumSystemVersion, NSPrincipalClass, NSHighResolutionCapable,
    # file-type declarations, and arch priority. CMake's default template
    # doesn't include those keys.
    add_executable(${EXE_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${EXE_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Notepatra"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.notepatra.editor"
        MACOSX_BUNDLE_INFO_STRING "Notepatra ${PROJECT_VERSION} — Native code editor"
        MACOSX_BUNDLE_COPYRIGHT "© Prateek Singh. GPL-3.0 licensed."
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
    )
    # Embed icon if present
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/notepatra.icns")
        set_source_files_properties(resources/notepatra.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${EXE_NAME} PRIVATE resources/notepatra.icns)
        set_target_properties(${EXE_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "notepatra.icns")
    endif()
    # Ship entitlements inside the bundle so install.sh can re-sign
    # the user's local copy without needing network access to fetch
    # them. macOS codesign reads this during the hardened-runtime
    # re-sign step in install.sh.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/entitlements.plist")
        set_source_files_properties(resources/entitlements.plist PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${EXE_NAME} PRIVATE resources/entitlements.plist)
    endif()
elseif(WIN32)
    # Windows: WIN32 flag suppresses console window
    add_executable(${EXE_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    # Linux: plain executable
    add_executable(${EXE_NAME} ${SOURCES} ${HEADERS})
endif()

add_dependencies(${EXE_NAME} rust_core)

# Inject the version from `project(Notepatra VERSION X.Y.Z)` so main.cpp's
# --version, --help, and app.setApplicationVersion() always match the build.
# Single source of truth: CMakeLists.txt → NOTEPATRA_VERSION compile define.
target_compile_definitions(${EXE_NAME} PRIVATE
    NOTEPATRA_VERSION="${PROJECT_VERSION}"
)

target_include_directories(${EXE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/rust-core/include
    ${QSCINTILLA_INCLUDE}
)

# ── Linking ──
target_link_libraries(${EXE_NAME} PRIVATE
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Network
    Qt5::Concurrent
    Qt5::Sql
    Qt5::Charts
    ${QSCINTILLA_LIB}
    ${RUST_LIB}
)

# v0.1.63 — link WebEngineWidgets when the renderer is enabled. The
# stub path (NOTEPATRA_WITH_WEBENGINE=OFF) doesn't include any
# WebEngine header, so the link skip is required as well as the
# compile define (see src/charts/vega_chart_renderer.cpp), so the stub
# build neither links nor includes any WebEngine header.
target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/charts)
if(NOTEPATRA_WITH_WEBENGINE)
    target_compile_definitions(${EXE_NAME} PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
    target_link_libraries(${EXE_NAME} PRIVATE Qt5::WebEngineWidgets)
endif()

if(WIN32)
    # CRITICAL: QScintilla on Windows is built as a DLL by default. Its headers
    # export symbols via __declspec(dllexport)/dllimport guarded by QSCINTILLA_DLL.
    # Consumers MUST define QSCINTILLA_DLL so the headers emit dllimport on
    # their declarations — otherwise the linker looks for QsciScintilla::staticMetaObject
    # in our own .obj files (where it isn't) instead of the import library.
    target_compile_definitions(${EXE_NAME} PRIVATE QSCINTILLA_DLL)

    # Rust staticlib needs these system libs on MSVC. Rust normally emits link
    # directives for them but being explicit avoids surprises across toolchains.
    target_link_libraries(${EXE_NAME} PRIVATE
        ws2_32 userenv bcrypt ntdll
        advapi32 ole32 shell32 dbghelp
        kernel32 user32 gdi32 winspool comdlg32
    )
elseif(APPLE)
    target_link_libraries(${EXE_NAME} PRIVATE pthread dl m
        "-framework Security" "-framework CoreFoundation")
else()
    target_link_libraries(${EXE_NAME} PRIVATE pthread dl m)
endif()

# ── DuckDB (optional, vendored) ──
# v0.1.55 — DuckDB is the universal-reader engine for the Data Analyst
# mode: CSV / Parquet / JSON / S3 / SQLite federation, all behind one SQL
# dialect. We detect a vendored copy at vendor/duckdb/ first; if missing,
# fall back to a system-installed libduckdb; if neither exists, build
# without DuckDB and the connection panel hides the DuckDB option.
#
# Vendored layout (after `cd vendor/duckdb && unzip libduckdb-*.zip`):
#   vendor/duckdb/duckdb.h
#   vendor/duckdb/libduckdb.so         (Linux x64)
#   vendor/duckdb/libduckdb.dylib      (macOS arm64/x64)
#   vendor/duckdb/duckdb.dll, duckdb.lib (Windows x64)
#
# Runtime: an RPATH of $ORIGIN tells the loader to look for
# libduckdb.so next to the notepatra binary so we can ship them together.
option(NOTEPATRA_USE_DUCKDB "Enable DuckDB-backed connection types in Data mode" ON)
if(NOTEPATRA_USE_DUCKDB)
    set(_DUCKDB_VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor/duckdb")
    find_path(DUCKDB_INCLUDE_DIR
        NAMES duckdb.h
        HINTS "${_DUCKDB_VENDOR_DIR}"
        PATHS /usr/include /usr/local/include /opt/homebrew/include
    )
    if(WIN32)
        set(_DUCKDB_LIB_NAMES duckdb)
    elseif(APPLE)
        set(_DUCKDB_LIB_NAMES duckdb libduckdb)
    else()
        set(_DUCKDB_LIB_NAMES duckdb libduckdb)
    endif()
    find_library(DUCKDB_LIBRARY
        NAMES ${_DUCKDB_LIB_NAMES}
        HINTS "${_DUCKDB_VENDOR_DIR}"
        PATHS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu
              /opt/homebrew/lib /opt/local/lib
    )
    if(DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        message(STATUS "DuckDB: found at ${DUCKDB_LIBRARY}")
        target_compile_definitions(${EXE_NAME} PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(${EXE_NAME} PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(${EXE_NAME} PRIVATE ${DUCKDB_LIBRARY})
        # Tell the runtime loader to look next to the binary first.
        if(UNIX AND NOT APPLE)
            set_target_properties(${EXE_NAME} PROPERTIES
                INSTALL_RPATH "$ORIGIN"
                BUILD_RPATH   "${_DUCKDB_VENDOR_DIR}"
            )
        elseif(APPLE)
            set_target_properties(${EXE_NAME} PROPERTIES
                INSTALL_RPATH "@executable_path"
                BUILD_RPATH   "${_DUCKDB_VENDOR_DIR}"
            )
        endif()
    else()
        message(STATUS "DuckDB: not found — Data-mode DuckDB connection types will be disabled at runtime")
    endif()
endif()

# ── Regression tests (opt in via -DBUILD_TESTING=ON) ──
# BUILD_LEXER_TEST remains as a compatibility alias for old scripts/docs.
#
# Each call to notepatra_add_qt_test() also appends the target name to the
# global property `notepatra_test_targets`. After all tests are registered
# below, we expose them as a single `notepatra_all_tests` meta-target so
# CI's `cmake --build . --target notepatra_all_tests` builds every test
# without having to manually keep build.yml in sync with new test_*.cpp
# files. v0.1.57 hit a CI regression because test_edit_plan.cpp existed
# in CMake but wasn't in build.yml's --target list — this prevents that
# class of failure forever.
function(notepatra_add_qt_test target_name)
    add_test(
        NAME ${target_name}
        COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:${target_name}>
    )
    set_property(GLOBAL APPEND PROPERTY notepatra_test_targets ${target_name})
endfunction()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_terminal_ansi.cpp")
    add_executable(test_terminal_ansi test_terminal_ansi.cpp)
    target_link_libraries(test_terminal_ansi PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_terminal_ansi)
    set_tests_properties(test_terminal_ansi PROPERTIES LABELS "smoke;terminal;v0125")
    message(STATUS "Regression test enabled — target: test_terminal_ansi")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_lexers_v0125.cpp")
    add_executable(test_lexers_v0125
        test_lexers_v0125.cpp
        src/lexerutils.cpp
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    target_include_directories(test_lexers_v0125 PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_lexers_v0125 PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_lexers_v0125 PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_lexers_v0125)
    set_tests_properties(test_lexers_v0125 PROPERTIES LABELS "smoke;lexer;v0125")
    message(STATUS "Regression test enabled — target: test_lexers_v0125")
endif()

# ── v0.1.58 release validation: lexer smoke test ──
# Builds tests/comprehensive/lexer_smoke.cpp which constructs a real
# QsciScintilla widget per language, attaches the lexer returned by
# Notepatra's createLexerForLanguage(), feeds it a keyword-rich snippet,
# and verifies Scintilla reports >=2 distinct styles AND some non-zero
# styled bytes — i.e. the lexer actually classified keywords as keywords.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/comprehensive/lexer_smoke.cpp")
    add_executable(test_lexer_smoke
        tests/comprehensive/lexer_smoke.cpp
        src/lexerutils.cpp
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    target_include_directories(test_lexer_smoke PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_lexer_smoke PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_lexer_smoke PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_lexer_smoke)
    set_tests_properties(test_lexer_smoke PROPERTIES LABELS "smoke;lexer;v0158")
    message(STATUS "Regression test enabled — target: test_lexer_smoke")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_lexers.cpp")
    add_executable(test_lexers test_lexers.cpp)
    target_include_directories(test_lexers PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_lexers PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::PrintSupport
        ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_lexers PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_lexers)
    set_tests_properties(test_lexers PROPERTIES LABELS "smoke;lexer")
    message(STATUS "Regression test enabled — target: test_lexers")
endif()

# ── Palette verification test ──
# Builds a test that verifies the EXACT Notepad++ default palette colors
# are applied to every QsciLexer — keywords #0000FF bold, comments #008000
# italic, numbers #FF8000, strings #808080, operators #000000 bold.
# This catches the Windows v0.1.1 bug where lexers tokenized correctly but
# rendered all styles as default black-on-white.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_context.cpp")
    add_executable(test_ai_context
        test_ai_context.cpp
        src/ai_context.cpp
    )
    target_include_directories(test_ai_context PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ai_context PRIVATE Qt5::Core)
    add_test(NAME test_ai_context COMMAND $<TARGET_FILE:test_ai_context>)
    set_tests_properties(test_ai_context PROPERTIES LABELS "unit;ai")
    message(STATUS "Regression test enabled — target: test_ai_context")
endif()

# v0.1.40 — AiIntent::detectFixIntent regression test. Validates the
# "fix my json/html/sql" intent classifier that AIPanel::sendPrompt
# uses to swap in the strict-patcher system prompt for that turn.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_intent.cpp")
    add_executable(test_ai_intent
        test_ai_intent.cpp
        src/ai_intent.cpp
    )
    target_include_directories(test_ai_intent PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ai_intent PRIVATE Qt5::Core)
    add_test(NAME test_ai_intent COMMAND $<TARGET_FILE:test_ai_intent>)
    set_tests_properties(test_ai_intent PROPERTIES LABELS "unit;ai")
    message(STATUS "Regression test enabled — target: test_ai_intent")
endif()

# v0.1.37 — comprehensive lexer-coverage check. Walks every QsciLexer
# subclass + every Notepatra-local lexer, applies palette in Light /
# Dark / Monokai, and asserts no non-Default style falls through to
# default text colour. Prevents the "X language has unstyled keywords"
# class of bug.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_lexer_coverage.cpp")
    add_executable(test_lexer_coverage
        test_lexer_coverage.cpp
        src/npp_palette.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_go.cpp
        src/lexer_swift.cpp
        src/lexer_kotlin.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    target_include_directories(test_lexer_coverage PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_lexer_coverage PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport
        ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_lexer_coverage PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_lexer_coverage)
    set_tests_properties(test_lexer_coverage PROPERTIES LABELS "regression;palette")
    message(STATUS "Regression test enabled — target: test_lexer_coverage")
endif()

# v0.1.35 — AI tool-call path-safety + execution tests.
# v0.1.43 — also pulls in csvanalyst + dbconnections (used by query_sql /
# csv_query) so the test target links against Qt SQL.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_tools.cpp")
    add_executable(test_ai_tools
        test_ai_tools.cpp
        src/ai_tools.cpp
        src/git_tools.cpp
        src/csvanalyst.cpp
        src/dbconnections.cpp
    )
    target_include_directories(test_ai_tools PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ai_tools PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql)
    add_test(NAME test_ai_tools COMMAND $<TARGET_FILE:test_ai_tools>)
    set_tests_properties(test_ai_tools PROPERTIES LABELS "unit;ai;tools")
    message(STATUS "Regression test enabled — target: test_ai_tools")
endif()

# v0.1.58 — End-to-end git-tools harness. Spins up a temp git repo, runs
# every git_* tool through AiTools::execute, asserts on the JSON wire
# shape, and exercises the error paths (no_workspace, not_a_repo, traversal,
# malformed commit refs). Also sweeps the live Notepatra repo read-only.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_git_tools_e2e.cpp")
    add_executable(test_git_tools_e2e
        test_git_tools_e2e.cpp
        src/ai_tools.cpp
        src/git_tools.cpp
        src/csvanalyst.cpp
        src/dbconnections.cpp
    )
    target_include_directories(test_git_tools_e2e PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_git_tools_e2e PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql)
    add_test(NAME test_git_tools_e2e COMMAND $<TARGET_FILE:test_git_tools_e2e>)
    set_tests_properties(test_git_tools_e2e PROPERTIES LABELS "integration;ai;tools;git")
    message(STATUS "Regression test enabled — target: test_git_tools_e2e")
endif()

# v0.1.43 — Data Analyst Mode regression suite. Pure-logic tests for the
# DataAnalyst intent + CSV analyst + DB connections + chart renderer +
# model capability gating. Runs offscreen so QChartView can construct.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_dataanalyst.cpp")
    add_executable(test_ai_dataanalyst
        test_ai_dataanalyst.cpp
        src/ai_systemprompt.cpp
        src/ai_tools.cpp
        src/git_tools.cpp
        src/chartrender.cpp
        src/csvanalyst.cpp
        src/dbconnections.cpp
    )
    target_include_directories(test_ai_dataanalyst PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ai_dataanalyst PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql Qt5::Charts)
    add_test(NAME test_ai_dataanalyst COMMAND $<TARGET_FILE:test_ai_dataanalyst>)
    set_tests_properties(test_ai_dataanalyst PROPERTIES
        LABELS "unit;ai;data-analyst"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_ai_dataanalyst")
endif()

# ─── Project Search deep regression test ────────────────────────────────
# Exercises ProjectSearchWorker against a hermetic on-disk fixture:
# coordinate correctness, heavy-dir pruning, binary skip, unicode columns,
# whole-word regex, cancellation, and elapsed-ms reporting. Runs headless
# via QT_QPA_PLATFORM=offscreen (set inside the test binary).
# ─── Updater pure-logic deep test ───────────────────────────────────────
# Covers the two safety-critical PURE functions: asset picker and
# SHA256SUMS parser. Anything impure (network, file I/O, widgets) is
# exercised manually + integration-tested by running the binary — the
# pure pieces are where a silent wrong-install bug would actually hide.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_updater.cpp")
    add_executable(test_updater
        test_updater.cpp
        src/updater.cpp
    )
    target_include_directories(test_updater PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_updater PRIVATE
        Qt5::Widgets       # Updater uses QMessageBox/QProgressDialog
        Qt5::Network
    )
    add_test(NAME test_updater COMMAND $<TARGET_FILE:test_updater>)
    set_tests_properties(test_updater PROPERTIES
        LABELS "unit;updater"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    message(STATUS "Regression test enabled — target: test_updater")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_projectsearch.cpp")
    add_executable(test_projectsearch
        test_projectsearch.cpp
        src/projectsearch.cpp
        src/rustbridge.cpp
    )
    target_include_directories(test_projectsearch PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
    )
    target_link_libraries(test_projectsearch PRIVATE
        Qt5::Widgets        # projectsearch.cpp defines ProjectSearch (QWidget)
        Qt5::Concurrent
        ${RUST_LIB}
    )
    if(WIN32)
        # Rust staticlib needs these system libs on MSVC — same set the
        # main notepatra target links against. Without ntdll the stdlib's
        # sync I/O references go unresolved (LNK2019 NtWriteFile etc.).
        target_link_libraries(test_projectsearch PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32
        )
    elseif(APPLE)
        target_link_libraries(test_projectsearch PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_projectsearch PRIVATE pthread dl m)
    endif()
    add_dependencies(test_projectsearch rust_core)
    add_test(NAME test_projectsearch COMMAND $<TARGET_FILE:test_projectsearch>)
    set_tests_properties(test_projectsearch PROPERTIES
        LABELS "unit;search"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    message(STATUS "Regression test enabled — target: test_projectsearch")
endif()

# Headless UI smoke test — proves the ProjectSearch widget's status
# label cycles through the walk → scan → finish text states and that
# the progress bar fills 0→100 without bouncing. Bypasses xdotool.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_projectsearch_ui.cpp")
    add_executable(test_projectsearch_ui
        test_projectsearch_ui.cpp
        src/projectsearch.cpp
        src/rustbridge.cpp
    )
    target_include_directories(test_projectsearch_ui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
    )
    target_link_libraries(test_projectsearch_ui PRIVATE
        Qt5::Widgets
        Qt5::Concurrent
        ${RUST_LIB}
    )
    if(WIN32)
        target_link_libraries(test_projectsearch_ui PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32
        )
    elseif(APPLE)
        target_link_libraries(test_projectsearch_ui PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_projectsearch_ui PRIVATE pthread dl m)
    endif()
    add_dependencies(test_projectsearch_ui rust_core)
    add_test(NAME test_projectsearch_ui COMMAND $<TARGET_FILE:test_projectsearch_ui>)
    set_tests_properties(test_projectsearch_ui PROPERTIES
        LABELS "ui;search"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    message(STATUS "Regression test enabled — target: test_projectsearch_ui")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ollama.cpp")
    add_executable(test_ollama
        test_ollama.cpp
        src/ollama.cpp
    )
    target_include_directories(test_ollama PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ollama PRIVATE
        Qt5::Core
        Qt5::Network
    )
    add_test(NAME test_ollama COMMAND $<TARGET_FILE:test_ollama>)
    set_tests_properties(test_ollama PROPERTIES LABELS "integration;ollama")
    message(STATUS "Regression test enabled — target: test_ollama")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_llamacpp.cpp")
    add_executable(test_llamacpp
        test_llamacpp.cpp
        src/ollama.cpp
    )
    target_include_directories(test_llamacpp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_llamacpp PRIVATE
        Qt5::Core
        Qt5::Network
    )
    add_test(NAME test_llamacpp COMMAND $<TARGET_FILE:test_llamacpp>)
    set_tests_properties(test_llamacpp PROPERTIES LABELS "integration;llamacpp;openai-compat")
    message(STATUS "Regression test enabled — target: test_llamacpp")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_aifix.cpp")
    add_executable(test_aifix
        test_aifix.cpp
        src/ollama.cpp
    )
    target_include_directories(test_aifix PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_aifix PRIVATE
        Qt5::Core
        Qt5::Network
    )
    add_test(NAME test_aifix COMMAND $<TARGET_FILE:test_aifix>)
    set_tests_properties(test_aifix PROPERTIES LABELS "integration;ollama;aifix")
    message(STATUS "Regression test enabled — target: test_aifix")
endif()

# v0.1.55 — credential scrubber test. Validates that the redactor catches
# every vendor pattern we know about and never lets a real-shape secret
# through to the model.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_credscrub.cpp")
    add_executable(test_credscrub
        test_credscrub.cpp
        src/credscrub.cpp
    )
    target_include_directories(test_credscrub PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_credscrub PRIVATE
        Qt5::Core
    )
    add_test(NAME test_credscrub COMMAND $<TARGET_FILE:test_credscrub>)
    set_tests_properties(test_credscrub PROPERTIES LABELS "unit;security;credscrub")
    message(STATUS "Security test enabled — target: test_credscrub")
endif()

# v0.2 Coding-mode revamp — smoke test for EditPlanList + DiffView. Builds
# only the two new sources plus rustbridge (the Rust diff path) so it stays
# cheap and never accidentally links aipanel.cpp (which would drag in the
# whole UI tree).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_edit_plan.cpp")
    add_executable(test_edit_plan
        test_edit_plan.cpp
        src/edit_plan.cpp
        src/diff_view.cpp
        src/rustbridge.cpp
    )
    add_dependencies(test_edit_plan rust_core)
    target_include_directories(test_edit_plan PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
    )
    target_link_libraries(test_edit_plan PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        ${RUST_LIB}
    )
    if(WIN32)
        # Rust staticlib pulls in winsock / crypto / ntdll. Same set the
        # main notepatra target links against — without these the stdlib's
        # sync I/O symbols go unresolved.
        target_link_libraries(test_edit_plan PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32
        )
    elseif(APPLE)
        target_link_libraries(test_edit_plan PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_edit_plan PRIVATE pthread dl m)
    endif()
    notepatra_add_qt_test(test_edit_plan)
    set_tests_properties(test_edit_plan PROPERTIES LABELS "smoke;ui;coding-mode")
    message(STATUS "Smoke test enabled — target: test_edit_plan")
endif()

# v0.1.55 — DuckDB smoke test. Validates RAII handle lifecycle, streaming
# row callback, schema introspection, error path, and CSV register-then-
# query. Skipped automatically if NOTEPATRA_USE_DUCKDB is OFF or libduckdb
# wasn't found at configure time.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_duckdb.cpp"
   AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
    add_executable(test_duckdb
        test_duckdb.cpp
        src/duckdb_client.cpp
    )
    target_include_directories(test_duckdb PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${DUCKDB_INCLUDE_DIR}
    )
    target_compile_definitions(test_duckdb PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
    target_link_libraries(test_duckdb PRIVATE
        Qt5::Core
        ${DUCKDB_LIBRARY}
    )
    if(UNIX AND NOT APPLE)
        set_target_properties(test_duckdb PROPERTIES
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/vendor/duckdb"
        )
    elseif(APPLE)
        set_target_properties(test_duckdb PROPERTIES
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/vendor/duckdb"
        )
    endif()
    add_test(NAME test_duckdb COMMAND $<TARGET_FILE:test_duckdb>)
    set_tests_properties(test_duckdb PROPERTIES LABELS "unit;duckdb;data")
    message(STATUS "DuckDB smoke test enabled — target: test_duckdb")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_fmtpanel_diff.cpp")
    add_executable(test_fmtpanel_diff
        test_fmtpanel_diff.cpp
        src/fmtpanel.cpp
        src/npp_palette.cpp
    )
    target_include_directories(test_fmtpanel_diff PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_fmtpanel_diff PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::PrintSupport
        ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_fmtpanel_diff PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_fmtpanel_diff)
    set_tests_properties(test_fmtpanel_diff PROPERTIES LABELS "regression;ui")
    message(STATUS "Regression test enabled — target: test_fmtpanel_diff")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_compare_widget.cpp")
    add_executable(test_compare_widget
        test_compare_widget.cpp
        src/compare.cpp
        src/lexerutils.cpp
        src/npp_palette.cpp
        src/rustbridge.cpp
        # v0.1.26: lexerutils.cpp now references these custom lexer
        # subclasses for PowerShell / Rust / Go / Swift / TS / Kotlin.
        # Without compiling them in, the linker can't find their vtables
        # or constructors. Same fix applies to every test target that
        # links lexerutils.cpp.
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    add_dependencies(test_compare_widget rust_core)
    target_include_directories(test_compare_widget PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_compare_widget PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::PrintSupport
        ${QSCINTILLA_LIB}
        ${RUST_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_compare_widget PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_compare_widget PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32
        )
    elseif(APPLE)
        target_link_libraries(test_compare_widget PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_compare_widget PRIVATE pthread dl m)
    endif()
    notepatra_add_qt_test(test_compare_widget)
    set_tests_properties(test_compare_widget PROPERTIES LABELS "regression;compare;ui")
    message(STATUS "Regression test enabled — target: test_compare_widget")
endif()

# v0.1.42 — integration tests that programmatically toggle Preferences
# controls + verify the active editor's actual state changes. This is
# the test suite that would have caught the v0.1.41 stub-Preferences
# regression.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_options_actually_work.cpp")
    add_executable(test_options_actually_work
        test_options_actually_work.cpp
        src/preferences.cpp
        src/editor.cpp
        src/gitgutter.cpp
        src/git_hunk_apply.cpp
        src/gutter_hunk_popup.cpp
        src/diff_view.cpp
        src/lexerutils.cpp
        src/npp_palette.cpp
        src/rustbridge.cpp
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    add_dependencies(test_options_actually_work rust_core)
    target_include_directories(test_options_actually_work PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_options_actually_work PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::PrintSupport
        ${QSCINTILLA_LIB}
        ${RUST_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_options_actually_work PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_options_actually_work PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_options_actually_work PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_options_actually_work PRIVATE pthread dl m)
    endif()
    notepatra_add_qt_test(test_options_actually_work)
    set_tests_properties(test_options_actually_work PROPERTIES LABELS "regression;preferences;options")
    message(STATUS "Regression test enabled — target: test_options_actually_work")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_palette.cpp")
    add_executable(test_palette
        test_palette.cpp
        src/npp_palette.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_go.cpp
        src/lexer_swift.cpp
        src/lexer_kotlin.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
    )
    target_include_directories(test_palette PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_palette PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::PrintSupport
        ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_palette PRIVATE QSCINTILLA_DLL)
    endif()
    # Pass the source dir as a string macro so the structural fold-margin
    # check (v0.1.34) can read sqlfmtpanel.cpp / fmtpanel.cpp / compare.cpp /
    # editor.cpp at test time and verify each pairs setFolding() with
    # setFoldMarginColors().
    target_compile_definitions(test_palette PRIVATE
        SOURCE_DIR_FOR_TEST="${CMAKE_SOURCE_DIR}")
    notepatra_add_qt_test(test_palette)
    set_tests_properties(test_palette PROPERTIES LABELS "regression;palette")
    message(STATUS "Regression test enabled — target: test_palette")
endif()

# ─── SQL formatter regression test ──────────────────────────────────────
# Drives the spec for the upcoming Claude-style pretty-printer + AI Fix.
# Handles either the 3-arg (legacy) or 4-arg (dialect-aware) formatSql
# signature — selected at configure time by grep'ing rustbridge.h.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_sqlfmt.cpp")
    add_executable(test_sqlfmt
        test_sqlfmt.cpp
        src/rustbridge.cpp
    )
    add_dependencies(test_sqlfmt rust_core)
    target_include_directories(test_sqlfmt PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
    )
    target_link_libraries(test_sqlfmt PRIVATE
        Qt5::Core
        ${RUST_LIB}
    )
    # Detect whether rustbridge.h exposes the 4-arg formatSql (with dialect)
    # by grep'ing the header. Lets the test compile against either shape.
    file(READ "${CMAKE_SOURCE_DIR}/src/rustbridge.h" _rb_hdr)
    if(_rb_hdr MATCHES "formatSql[^;]*dialect")
        target_compile_definitions(test_sqlfmt PRIVATE SQLFMT_HAS_DIALECT=1)
        message(STATUS "test_sqlfmt: SQLFMT_HAS_DIALECT enabled")
    endif()
    if(WIN32)
        target_link_libraries(test_sqlfmt PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32
        )
    elseif(APPLE)
        target_link_libraries(test_sqlfmt PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_sqlfmt PRIVATE pthread dl m)
    endif()
    add_test(NAME test_sqlfmt COMMAND $<TARGET_FILE:test_sqlfmt>)
    set_tests_properties(test_sqlfmt PROPERTIES
        LABELS "regression;sqlfmt"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
    message(STATUS "Regression test enabled — target: test_sqlfmt")
endif()

# ── Install ──
if(APPLE)
    install(TARGETS ${EXE_NAME} BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS ${EXE_NAME} RUNTIME DESTINATION bin)
else()
    install(TARGETS ${EXE_NAME} DESTINATION bin)
endif()

# ── notepatra_all_tests meta-target ──
# Auto-discovers every executable target named test_* and aggregates them
# under one custom target. CI uses `cmake --build . --target
# notepatra_all_tests` so adding a new test_xxx.cpp + add_executable just
# works in CI without touching build.yml.
#
# v0.1.63 — Vega-Lite chart renderer regression test.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_vega_chart.cpp")
    add_executable(test_vega_chart
        test_vega_chart.cpp
        src/ai_tools.cpp
        src/git_tools.cpp
        src/csvanalyst.cpp
        src/dbconnections.cpp
        src/charts/vega_chart_renderer.cpp
        src/plugin_loader.cpp
    )
    target_include_directories(test_vega_chart PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
    )
    target_link_libraries(test_vega_chart PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_vega_chart PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_vega_chart PRIVATE Qt5::WebEngineWidgets)
    endif()
    add_test(NAME test_vega_chart COMMAND
        ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:test_vega_chart>)
    set_tests_properties(test_vega_chart PROPERTIES
        LABELS "unit;ai;data-analyst;charts;v0163"
    )
    message(STATUS "Regression test enabled — target: test_vega_chart")
endif()

# v0.1.57 hit a CI regression because test_edit_plan was registered with
# CTest but missing from build.yml's hardcoded --target list — this
# discovery loop prevents that class of failure forever.
if(NOTEPATRA_BUILD_TESTS)
    get_property(_all_targets DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY BUILDSYSTEM_TARGETS)
    set(_test_targets "")
    foreach(_t IN LISTS _all_targets)
        if(_t MATCHES "^test_")
            get_target_property(_type ${_t} TYPE)
            if(_type STREQUAL "EXECUTABLE")
                list(APPEND _test_targets ${_t})
            endif()
        endif()
    endforeach()
    if(_test_targets)
        add_custom_target(notepatra_all_tests DEPENDS ${_test_targets})
        list(LENGTH _test_targets _n)
        message(STATUS "Aggregated ${_n} test_* executables into notepatra_all_tests")
    endif()
endif()
