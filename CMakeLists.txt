cmake_minimum_required(VERSION 3.16)
project(Notepatra VERSION 0.1.124 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)   # compile .qrc in target SOURCES (icons/vega/diagram render layer)
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

# v0.1.107 — Full-flavor umbrella. Turns on the heavy opt-in features:
# WebEngine (inline Vega-Lite charts, WHERE the Qt module is available) and
# the DuckDB engine. The default (Lite) build has neither. On macOS, Homebrew
# qt@5 no longer ships QtWebEngine, so a Full macOS build gets DuckDB only and
# the WebEngine probe below downgrades to a stub — that is expected and fine.
option(NOTEPATRA_FULL "Full flavor: WebEngine charts (where available) + DuckDB engine" OFF)
if(NOTEPATRA_FULL)
    set(NOTEPATRA_WITH_WEBENGINE ON)
endif()

# v0.1.72 — Cloud-free build variant. When ON, the binary physically
# refuses to talk to any non-private network destination (i.e. anything
# that's not localhost / 127.x / [::1] / 10.x / 172.16-31.x / 192.168.x /
# *.local / *.lan / unix sockets). The cloud-URL paste UI is also
# stripped so the user can't even type api.openai.com — there's no path
# from the binary to a public LLM endpoint.
#
# Shipped as the `notepatra-local-ai` .deb for regulated industries,
# air-gapped fleets, data-sovereignty compliance.  Ollama / llama.cpp /
# LM Studio / Jan / self-hosted Ollama on the LAN all continue to work.
option(NOTEPATRA_NO_CLOUD "Build cloud-free variant — block all non-private LLM endpoints" OFF)
if(NOTEPATRA_NO_CLOUD)
    add_compile_definitions(NOTEPATRA_NO_CLOUD=1)
    message(STATUS "Building cloud-free variant: public LLM endpoints will be refused at runtime")
endif()

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
    # v0.1.113: VS2026 (MSVC toolset 14.5x / _MSC_VER >= 1950) removed
    # stdext::make_checked_array_iterator, which Qt 5.15.2's qlist.h still calls
    # via QT_MAKE_CHECKED_ARRAY_ITERATOR — so every QList-instantiating TU fails
    # to compile (C2653 / C3861). GitHub's hosted Windows images all rolled to
    # VS2026 with no VS2022 left, so it can't be fixed at the runner level.
    # Force-include a one-line shim before every TU's Qt headers. The shim is a
    # no-op (#if-guarded out) on every other toolchain. See src/msvc_stdext_shim.h.
    add_compile_options(/FI${CMAKE_CURRENT_SOURCE_DIR}/src/msvc_stdext_shim.h)
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
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Concurrent Sql Charts Test)
# v0.1.70 — Qt5Svg is OPTIONAL: probe quietly, link if available. Without
# the dev package, QImageReader still decodes SVG via the runtime plugin
# (libqt5svg5) that ships with most distros. If neither is present the
# icons render as broken-image placeholders — acceptable degradation.
find_package(Qt5 QUIET COMPONENTS Svg)

# v0.1.63 — optional QtWebEngine for Vega-Lite chart rendering. On distros
# that don't ship libqt5webengine5 (e.g. minimal CI containers), the option
# can be flipped off and VegaChartRenderer compiles a QLabel stub instead.
# We probe via find_package(... QUIET) so a missing component downgrades
# NOTEPATRA_WITH_WEBENGINE to OFF with a warning rather than failing the
# configure outright.
if(NOTEPATRA_WITH_WEBENGINE)
    find_package(Qt5 QUIET COMPONENTS WebEngineWidgets WebChannel)
    if(Qt5WebEngineWidgets_FOUND AND Qt5WebChannel_FOUND)
        message(STATUS "Vega-Lite renderer + Noter bridge: Qt5WebEngineWidgets ${Qt5WebEngineWidgets_VERSION} + Qt5WebChannel ${Qt5WebChannel_VERSION}")
    else()
        message(WARNING
            "Qt5WebEngineWidgets / Qt5WebChannel not found — Vega-Lite + Noter "
            "rich-editor will compile a stub. Install libqt5webengine5-dev + "
            "libqt5webchannel5-dev (Debian/Ubuntu), qt5-webengine + qt5-webchannel "
            "(Arch), qt@5 includes both on macOS. To silence this warning "
            "configure with -DNOTEPATRA_WITH_WEBENGINE=OFF.")
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

# .npd diagram subsystem. npd_parser.cpp is pure C++ (Qt Core only);
# diagram_render.cpp + diagram_view.cpp render natively with QPainter (no
# WebEngine) so the diagram works in BOTH flavors on every platform.
file(GLOB DIAGRAM_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/diagram/*.cpp")
list(APPEND SOURCES ${DIAGRAM_SOURCES})

# v0.1.70 — bundle 14 monochrome SVG icons used by AIPanel welcome cards,
# attachment chips, lock toggles, etc. Replaces the previous emoji glyphs
# (🤖 🌿 🔌 🖼 …) which rendered as tofu on Linux systems without color-
# emoji fonts. Qt's QImageReader (via Qt5::Svg) decodes the icons; HTML
# <img src=':/icons/foo.svg' width='14' height='14'> embeds them inline
# in QLabel rich text.
list(APPEND SOURCES resources/icons.qrc)

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
    src/lexer_plaintext.h
    src/lexer_csv.h
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
    src/inlineedit_diff.h
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
    # .npd diagram view host (Q_OBJECT — native QPainter renderer, both flavors).
    src/diagram/diagram_view.h
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
if(Qt5Svg_FOUND)
    target_link_libraries(${EXE_NAME} PRIVATE Qt5::Svg)
    # Enables DiagramView's SVG + HTML export (QSvgGenerator). PNG/PDF always work.
    target_compile_definitions(${EXE_NAME} PRIVATE NOTEPATRA_WITH_SVG)
endif()

# v0.1.63 — link WebEngineWidgets when the renderer is enabled. The
# stub path (NOTEPATRA_WITH_WEBENGINE=OFF) doesn't include any
# WebEngine header, so the link skip is required as well as the
# compile define (see src/charts/vega_chart_renderer.cpp), so the stub
# build neither links nor includes any WebEngine header.
target_include_directories(${EXE_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src/charts ${CMAKE_SOURCE_DIR}/src/diagram)
if(NOTEPATRA_WITH_WEBENGINE)
    target_compile_definitions(${EXE_NAME} PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
    target_link_libraries(${EXE_NAME} PRIVATE Qt5::WebEngineWidgets Qt5::WebChannel)
    # v0.1.90 — offline vega-embed bundle. ~828 KB of JS shipped as a
    # Qt resource so charts render without JSDelivr in the loop (works
    # air-gapped + survives CDN outages). Full flavor only — the lite
    # build doesn't include the WebEngine renderer at all.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/vega.qrc")
        target_sources(${EXE_NAME} PRIVATE resources/vega.qrc)
    endif()
    # v0.1.103 — the diagram render layer (dagre/render.js/diagram.html qrc) is
    # GONE: DiagramView now paints natively with QPainter (DiagramRender), so no
    # WebEngine + no qrc:///diagram/. resources/diagram/* is retained in-tree for
    # reference but no longer compiled into any binary.
    # v0.1.95 — Noter WebChannel JS resource removed alongside the
    # WebEngine path (notes_bridge / notes_editor) when the feature was
    # rewritten to use a vanilla QTextEdit. Leaving the EXISTS guard
    # here as historical breadcrumb in case a future WebEngine path comes back.
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
    # libxcb is a transitive Qt5 GUI dep on Linux X11. We use it directly
    # in main.cpp (startup-notify completion) and mainwindow.cpp
    # (_NET_ACTIVE_WINDOW with source=2 to bypass Cinnamon/Muffin's
    # focus-stealing prevention on IPC file-open), so link explicitly.
    target_link_libraries(${EXE_NAME} PRIVATE pthread dl m xcb)
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
# DuckDB is FULL-ONLY. libduckdb is ~57 MB, so the default Lite binary must
# never link it (hard rule: the bare binary stays small). It is enabled in the
# Full flavor — via the Full umbrella (NOTEPATRA_FULL, used where WebEngine is
# unavailable, e.g. macOS where Homebrew qt@5 dropped WebEngine) OR alongside
# WebEngine on Linux/Windows. Lite (neither) forces it OFF even if a caller
# passes -DNOTEPATRA_USE_DUCKDB=ON. The Data Analyst works on CSV + in-memory
# SQLite without DuckDB; with DuckDB it gains in-process Parquet/JSON/S3/SQLite.
if(NOTEPATRA_FULL OR NOTEPATRA_WITH_WEBENGINE)
    set(NOTEPATRA_USE_DUCKDB ON)
else()
    if(NOTEPATRA_USE_DUCKDB)
        message(WARNING "NOTEPATRA_USE_DUCKDB ignored: DuckDB is Full-only. Build the "
                        "Full flavor (-DNOTEPATRA_FULL=ON). Forcing OFF for the Lite binary.")
    endif()
    set(NOTEPATRA_USE_DUCKDB OFF)
endif()
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
        # NOTE: CI ships build-full/notepatra directly (no `make install`), so
        # the BUILD rpath — not just INSTALL_RPATH — must carry $ORIGIN /
        # @executable_path, else the shipped binary can't find libduckdb sitting
        # next to it (the bare build-tree rpath was the absolute vendor path +
        # an empty/cwd entry, which only resolved when cwd == the binary dir).
        # The vendor dir is kept as a second entry for local dev where the lib
        # lives under vendor/duckdb/.
        if(UNIX AND NOT APPLE)
            set_target_properties(${EXE_NAME} PROPERTIES
                INSTALL_RPATH "$ORIGIN"
                BUILD_RPATH   "$ORIGIN;${_DUCKDB_VENDOR_DIR}"
            )
        elseif(APPLE)
            set_target_properties(${EXE_NAME} PROPERTIES
                INSTALL_RPATH "@executable_path"
                BUILD_RPATH   "@executable_path;${_DUCKDB_VENDOR_DIR}"
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

# ── Non-printing character display (Notepad++ "Show Symbol" parity) ─────────
# Links only editor_symbols.cpp, not the app: the assertions measure real
# laid-out pixel widths in a live QsciScintilla, and pulling in the Rust core
# would make a fast, purely-visual test slow for no benefit.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_editor_symbols.cpp")
    add_executable(test_editor_symbols
        test_editor_symbols.cpp
        ${CMAKE_SOURCE_DIR}/src/editor_symbols.cpp)
    target_include_directories(test_editor_symbols PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_editor_symbols PRIVATE
        Qt5::Widgets Qt5::Test ${QSCINTILLA_LIB})
    if(WIN32)
        target_compile_definitions(test_editor_symbols PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_editor_symbols)
    set_tests_properties(test_editor_symbols PROPERTIES
        LABELS "regression;editor;symbols;v01124")
    message(STATUS "Regression test enabled — target: test_editor_symbols")
endif()

# ── Indent guides keep the theme's colours (reserved STYLE_INDENTGUIDE) ─────
# Links the REAL Editor: the claim under test is that Editor::applyTheme
# reaches a reserved Scintilla style, and a test against a free helper would
# pass even if the theme path never called it. The source list is Editor's
# link closure only — no MainWindow, no AI/DB/chart stack.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_indent_guide_theme.cpp")
    add_executable(test_indent_guide_theme
        test_indent_guide_theme.cpp
        ${CMAKE_SOURCE_DIR}/src/editor.cpp
        ${CMAKE_SOURCE_DIR}/src/editor_symbols.cpp
        ${CMAKE_SOURCE_DIR}/src/npp_palette.cpp
        ${CMAKE_SOURCE_DIR}/src/lexerutils.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_csv.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_extras.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_go.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_kotlin.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_plaintext.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_powershell.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_rust.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_swift.cpp
        ${CMAKE_SOURCE_DIR}/src/lexer_typescript.cpp
        ${CMAKE_SOURCE_DIR}/src/rustbridge.cpp
        ${CMAKE_SOURCE_DIR}/src/gitgutter.cpp
        ${CMAKE_SOURCE_DIR}/src/git_hunk_apply.cpp
        ${CMAKE_SOURCE_DIR}/src/gutter_hunk_popup.cpp
        ${CMAKE_SOURCE_DIR}/src/diff_view.cpp)
    add_dependencies(test_indent_guide_theme rust_core)
    target_include_directories(test_indent_guide_theme PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_indent_guide_theme PRIVATE
        Qt5::Widgets Qt5::Test ${QSCINTILLA_LIB} ${RUST_LIB})
    if(WIN32)
        target_compile_definitions(test_indent_guide_theme PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_indent_guide_theme PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32)
    elseif(APPLE)
        target_link_libraries(test_indent_guide_theme PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_indent_guide_theme PRIVATE pthread dl m)
    endif()
    notepatra_add_qt_test(test_indent_guide_theme)
    set_tests_properties(test_indent_guide_theme PROPERTIES
        LABELS "regression;editor;theme;indentguide")
    message(STATUS "Regression test enabled — target: test_indent_guide_theme")
endif()

# ── Read-only SQL classifier gate (query_sql / csv_query security contract) ──
# Links the REAL DbConnections::classifySql / isReadOnlyQuery / isMutation from
# dbconnections.cpp (Qt-Core string logic; NOTEPATRA_HAVE_DUCKDB stays off for
# this target so the classifier is exercised in isolation, no DuckDB link).
# Qt5::Gui/Widgets/Sql are pulled in because dbconnections.cpp's TU also holds
# the connection-manager dialog + QSqlDatabase code. Offscreen-safe: pure
# string logic, never constructs a widget or modal.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_db_readonly_gate.cpp")
    add_executable(test_db_readonly_gate
        test_db_readonly_gate.cpp
        src/dbconnections.cpp
    )
    target_include_directories(test_db_readonly_gate PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_db_readonly_gate PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Sql)
    notepatra_add_qt_test(test_db_readonly_gate)
    set_tests_properties(test_db_readonly_gate PROPERTIES
        LABELS "unit;ai;db;security" TIMEOUT 120)
    message(STATUS "Regression test enabled — target: test_db_readonly_gate")
endif()

# ── .npd diagram parser (diagram subsystem; pure C++, no WebEngine) ──
# The parser is the source-of-truth engine the diagram canvas projects from,
# so it's covered here independently of the native QPainter renderer.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_npd_parser.cpp")
    add_executable(test_npd_parser test_npd_parser.cpp src/diagram/npd_parser.cpp)
    target_include_directories(test_npd_parser PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/diagram)
    target_link_libraries(test_npd_parser PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_npd_parser)
    set_tests_properties(test_npd_parser PROPERTIES LABELS "unit;diagram")
    message(STATUS "Regression test enabled — target: test_npd_parser")
endif()

# ── Mermaid → .npd importer test (pure; round-trips through Npd::parse) ──
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_mermaid_import.cpp")
    add_executable(test_mermaid_import test_mermaid_import.cpp
        src/diagram/mermaid_import.cpp src/diagram/npd_parser.cpp)
    target_include_directories(test_mermaid_import PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/diagram)
    target_link_libraries(test_mermaid_import PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_mermaid_import)
    set_tests_properties(test_mermaid_import PROPERTIES LABELS "unit;diagram")
endif()

# v0.1.78 — wire rust-core's cargo unit tests into ctest so the encoding /
# binary-detection regression tests run as part of release-check.sh.
# Includes the file_io UTF-16/32 BOM coverage added to fix the "UTF-16 LE
# .sql files flagged as binary" bug. cargo test in release mode mirrors what
# the production build links against, so we don't get a debug/release skew.
if(NOTEPATRA_BUILD_TESTS)
    add_test(NAME rust_core_tests
             COMMAND cargo test --release
             WORKING_DIRECTORY ${RUST_CORE_DIR})
    set_tests_properties(rust_core_tests PROPERTIES LABELS "unit;rust;encoding;v0178")
    message(STATUS "Regression test enabled — rust-core cargo test")
endif()

# ── Save As filter contract test (v0.1.87 follow-up) ──
# Asserts the user-visible end-to-end contract — picking Python in the Save As
# dropdown auto-suffixes the saved file with .py — through the same helpers
# the dialog uses. Pure-functional, no Qt event loop.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_save_as_filters.cpp")
    add_executable(test_save_as_filters
        test_save_as_filters.cpp
        src/lexerutils.cpp
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
        src/lexer_plaintext.cpp
        src/lexer_csv.cpp
    )
    target_include_directories(test_save_as_filters PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_save_as_filters PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::PrintSupport ${QSCINTILLA_LIB}
    )
    if(WIN32)
        target_compile_definitions(test_save_as_filters PRIVATE QSCINTILLA_DLL)
    endif()
    notepatra_add_qt_test(test_save_as_filters)
    set_tests_properties(test_save_as_filters PROPERTIES LABELS "smoke;saveas")
    message(STATUS "Regression test enabled — target: test_save_as_filters")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_last_dir.cpp")
    add_executable(test_last_dir test_last_dir.cpp)
    target_include_directories(test_last_dir PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_last_dir PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_last_dir)
    set_tests_properties(test_last_dir PROPERTIES LABELS "unit;saveas;lastdir")
    message(STATUS "Regression test enabled — target: test_last_dir")
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
        src/lexer_plaintext.cpp
        src/lexer_csv.cpp
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
        src/lexer_plaintext.cpp
        src/lexer_csv.cpp
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

# ── Noter — HTML storage layer test (27 cases) ──
# Validates atomic write, backup ring, XSS sanitizer, lock file,
# filename safety, validate-before-write, and the 12 reliability
# defenses documented in design/notes-ux/index.html SURFACE 13.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_storage.cpp")
    add_executable(test_notes_storage
        test_notes_storage.cpp
        src/notes_storage.cpp
        src/notes_template.cpp
    )
    target_include_directories(test_notes_storage PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_notes_storage PRIVATE Qt5::Core Qt5::Network Qt5::Test)
    add_test(NAME test_notes_storage COMMAND $<TARGET_FILE:test_notes_storage>)
    set_tests_properties(test_notes_storage PROPERTIES LABELS "unit;noter;storage")
    message(STATUS "Regression test enabled — target: test_notes_storage")
endif()

# ── Noter — SQLite todos + reminder engine test (172 cases) ──
# Validates schema migration, HTML action-block reindex round-trip,
# due-group classification across time boundaries, snooze + reminder
# state transitions, quick-todo creation, and the app-lifetime reminder
# service (persisted last-tick meta + unbounded catch-up digest).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_todos.cpp")
    add_executable(test_notes_todos
        test_notes_todos.cpp
        src/notes_todos.cpp
        src/notes_reminder.cpp
    )
    target_include_directories(test_notes_todos PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_notes_todos PRIVATE Qt5::Core Qt5::Sql Qt5::Test)
    add_test(NAME test_notes_todos COMMAND $<TARGET_FILE:test_notes_todos>)
    set_tests_properties(test_notes_todos PROPERTIES LABELS "unit;noter;todos")
    message(STATUS "Regression test enabled — target: test_notes_todos")
endif()

# ── Noter — sweep prompt builder + parser test (36 cases) ──
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_sweep_prompt.cpp")
    add_executable(test_notes_sweep_prompt
        test_notes_sweep_prompt.cpp
        src/notes_sweep_prompt.cpp
    )
    target_include_directories(test_notes_sweep_prompt PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_notes_sweep_prompt PRIVATE Qt5::Core Qt5::Test)
    add_test(NAME test_notes_sweep_prompt COMMAND $<TARGET_FILE:test_notes_sweep_prompt>)
    set_tests_properties(test_notes_sweep_prompt PROPERTIES LABELS "unit;noter;sweep")
endif()

# ── Noter — AI-Extract marked-region apply layer (v0.1.112) ──
# Headless (QGuiApplication — QTextDocument needs QtGui, not Widgets).
# Links the REAL storage sanitizer so the apply→toHtml→sanitizeBody→
# setHtml→findExtractRegion round trip is exercised end-to-end: that
# round trip is the strongest regression guard on the invisible anchor
# markers the replace-in-place re-run behavior depends on.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_extract_apply.cpp")
    add_executable(test_notes_extract_apply
        test_notes_extract_apply.cpp
        src/notes_extract_apply.cpp
        src/notes_sweep_prompt.cpp
        src/notes_storage.cpp
        src/notes_template.cpp
    )
    target_include_directories(test_notes_extract_apply PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_notes_extract_apply PRIVATE
        Qt5::Core Qt5::Gui Qt5::Network Qt5::Test)
    add_test(NAME test_notes_extract_apply COMMAND $<TARGET_FILE:test_notes_extract_apply>)
    set_tests_properties(test_notes_extract_apply PROPERTIES
        LABELS "unit;noter;extract"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_notes_extract_apply")
endif()

# ── Noter — slide-over panels test (18 cases) ──
# v0.1.112 — also hosts the Extract reliability-layer widget tests, which
# drive the REAL NotesPanel (busy-state machine + pre-flight probe), so it
# links the same source set as test_notes_panel_widget.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_panels.cpp")
    add_executable(test_notes_panels
        test_notes_panels.cpp
        src/notes.cpp
        src/notes_storage.cpp
        src/notes_template.cpp
        src/notes_todos.cpp
        src/notes_reminder.cpp
        src/notes_popout.cpp
        src/notes_sweep_dialog.cpp
        src/notes_sweep_prompt.cpp
        src/notes_extract_apply.cpp
        src/notes_export.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
    )
    target_include_directories(test_notes_panels PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_notes_panels PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Sql Qt5::Network Qt5::Test
        Qt5::PrintSupport)
    add_test(NAME test_notes_panels COMMAND $<TARGET_FILE:test_notes_panels>)
    set_tests_properties(test_notes_panels PROPERTIES LABELS "unit;noter;panels")
endif()

# ── Noter — NotesPanel widget integration test (v0.1.95) ──
# Heavy linkage: exercises the actual two-pane widget end-to-end via QTest
# keystrokes. Drives newMeetingNote / openNoteFile / markdown shortcuts /
# F4 toggle / Enter-on-checkbox behaviors that the unit tests can't reach.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_panel_widget.cpp")
    add_executable(test_notes_panel_widget
        test_notes_panel_widget.cpp
        src/notes.cpp
        src/notes_storage.cpp
        src/notes_template.cpp
        src/notes_todos.cpp
        src/notes_reminder.cpp
        src/notes_popout.cpp
        src/notes_sweep_dialog.cpp
        src/notes_sweep_prompt.cpp
        src/notes_extract_apply.cpp
        src/notes_export.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
    )
    target_include_directories(test_notes_panel_widget PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_notes_panel_widget PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Sql Qt5::Network Qt5::Test
        Qt5::PrintSupport)
    add_test(NAME test_notes_panel_widget COMMAND $<TARGET_FILE:test_notes_panel_widget>)
    set_tests_properties(test_notes_panel_widget PROPERTIES
        LABELS "integration;noter;widget"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_notes_panel_widget")
endif()

# ── Noter — render-dump harness (writes PNGs for visual inspection) ──
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_noter_render.cpp")
    add_executable(test_noter_render
        test_noter_render.cpp
        src/notes.cpp
        src/notes_storage.cpp
        src/notes_template.cpp
        src/notes_todos.cpp
        src/notes_reminder.cpp
        src/notes_popout.cpp
        src/notes_sweep_dialog.cpp
        src/notes_sweep_prompt.cpp
        src/notes_extract_apply.cpp
        src/notes_export.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
    )
    target_include_directories(test_noter_render PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_noter_render PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Sql Qt5::Network Qt5::Test
        Qt5::PrintSupport)
    # Originally a manual visual-dump tool; registered as a ctest since the
    # M6 discoverability gate (every Noter QShortcut must be documented in
    # the Help guide) made it pass/fail. PNG dumps still land in /tmp.
    add_test(NAME test_noter_render COMMAND $<TARGET_FILE:test_noter_render>)
    set_tests_properties(test_noter_render PROPERTIES
        LABELS "integration;noter;render"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ── Noter — PDF + Markdown export test (25 cases, Full edition only) ──
if(NOTEPATRA_BUILD_TESTS AND NOTEPATRA_WITH_WEBENGINE
   AND EXISTS "${CMAKE_SOURCE_DIR}/test_notes_export.cpp")
    add_executable(test_notes_export
        test_notes_export.cpp
        src/notes_export.cpp
    )
    target_include_directories(test_notes_export PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_notes_export PRIVATE
        Qt5::Core Qt5::Widgets Qt5::WebEngineWidgets Qt5::Test)
    target_compile_definitions(test_notes_export PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
    add_test(NAME test_notes_export COMMAND $<TARGET_FILE:test_notes_export>)
    set_tests_properties(test_notes_export PROPERTIES LABELS "unit;noter;export")
endif()

# ── Diagram — native-Qt render + export test (BOTH flavors; no WebEngine) ──
# v0.1.103: the diagram renders with QPainter (DiagramRender), so this test
# builds in Lite AND Full and needs no WebEngine. Renders known .npd offscreen
# and proves PNG + SVG export via the native pipeline.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_diagram_view.cpp")
    add_executable(test_diagram_view
        test_diagram_view.cpp
        src/diagram/diagram_view.cpp
        src/diagram/diagram_render.cpp
        src/diagram/npd_parser.cpp
    )
    target_include_directories(test_diagram_view PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/diagram)
    target_link_libraries(test_diagram_view PRIVATE
        Qt5::Core Qt5::Widgets Qt5::Test)
    if(Qt5Svg_FOUND)
        target_link_libraries(test_diagram_view PRIVATE Qt5::Svg)
        target_compile_definitions(test_diagram_view PRIVATE NOTEPATRA_WITH_SVG)
    endif()
    add_test(NAME test_diagram_view COMMAND $<TARGET_FILE:test_diagram_view>)
    set_tests_properties(test_diagram_view PROPERTIES LABELS "integration;diagram")
endif()

# ── npd_render — offscreen .npd → image CLI (native Qt; dev/docs utility) ──
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/npd_render_qt.cpp")
    add_executable(npd_render
        npd_render_qt.cpp
        src/diagram/diagram_view.cpp
        src/diagram/diagram_render.cpp
        src/diagram/npd_parser.cpp
    )
    target_include_directories(npd_render PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/diagram)
    target_link_libraries(npd_render PRIVATE
        Qt5::Core Qt5::Widgets)
    if(Qt5Svg_FOUND)
        target_link_libraries(npd_render PRIVATE Qt5::Svg)
        target_compile_definitions(npd_render PRIVATE NOTEPATRA_WITH_SVG)
    endif()
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
        src/chart_modal.cpp
        src/chart_spec_to_vega.cpp
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

# v0.1.67 — AIPanel three-vector chat-history regression test.
#
# Verifies the per-mode partitioning introduced in v0.1.67: Chat /
# Coding / Data each get an independent m_*Messages vector, switching
# modes swaps the active vector without cross-contamination, clearChat
# clears only the active one, and the on-disk JSON format
# ({version:2, chat:[...], coding:[...], data:[...]}) round-trips.
# Also exercises the v1 migration path (flat-array → chat vector).
#
# Constructs a real AIPanel under QT_QPA_PLATFORM=offscreen — it
# therefore needs the full notepatra source tree (every dependency
# AIPanel's constructor pulls in: ai_systemprompt, ai_tools,
# dbconnections, csvanalyst, edit_plan, vega_chart_renderer,
# plugin_loader, credscrub, etc.). We use the same SOURCES glob
# notepatra uses, MINUS src/main.cpp (so the test provides its own).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_chat_history.cpp")
    # Pull every src/*.cpp + src/charts/*.cpp via glob, then strip
    # main.cpp (the test has its own main). Matches the SOURCES list
    # the notepatra executable uses.
    set(AI_CHAT_HISTORY_SOURCES ${SOURCES})
    list(FILTER AI_CHAT_HISTORY_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_ai_chat_history
        test_ai_chat_history.cpp
        ${AI_CHAT_HISTORY_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_ai_chat_history rust_core)
    # AIPanel uses NOTEPATRA_VERSION in the Charts Pack install hand-off
    # — the same macro the main executable carries via target_compile_definitions.
    target_compile_definitions(test_ai_chat_history PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_ai_chat_history PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_ai_chat_history PRIVATE
        Qt5::Widgets
        Qt5::PrintSupport
        Qt5::Network
        Qt5::Concurrent
        Qt5::Sql
        Qt5::Charts
        ${QSCINTILLA_LIB}
        ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_ai_chat_history PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_ai_chat_history PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_ai_chat_history PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_ai_chat_history PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_ai_chat_history PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_ai_chat_history PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_ai_chat_history PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_ai_chat_history PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_ai_chat_history PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_ai_chat_history COMMAND $<TARGET_FILE:test_ai_chat_history>)
    set_tests_properties(test_ai_chat_history PROPERTIES
        LABELS "unit;ai;chat-history"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_ai_chat_history")
endif()

# ── AI write/SQL approval gate + atomic write + byte-exact rollback + truncation ──
# v0.1.114/v0.1.115 security contract. Links the REAL AIPanel + DbConnections
# (same full source set as test_ai_chat_history — main.cpp stripped) so it
# exercises the shipped writeFileAtomic / resolveApproval / undoLastApply /
# renderTruncationNotice, not a copy. Offscreen-safe: the approval gate is
# driven through host accessors, never a QMessageBox exec(). Explicit TIMEOUT
# (enable_testing has no default) so a wedged nested DB event loop can't hang CI.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_write_gate.cpp")
    set(AI_WRITE_GATE_SOURCES ${SOURCES})
    list(FILTER AI_WRITE_GATE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_ai_write_gate
        test_ai_write_gate.cpp
        ${AI_WRITE_GATE_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_ai_write_gate rust_core)
    target_compile_definitions(test_ai_write_gate PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_ai_write_gate PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_ai_write_gate PRIVATE
        Qt5::Widgets
        Qt5::PrintSupport
        Qt5::Network
        Qt5::Concurrent
        Qt5::Sql
        Qt5::Charts
        ${QSCINTILLA_LIB}
        ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_ai_write_gate PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_ai_write_gate PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_ai_write_gate PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_ai_write_gate PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_ai_write_gate PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_ai_write_gate PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_ai_write_gate PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_ai_write_gate PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_ai_write_gate PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_ai_write_gate COMMAND $<TARGET_FILE:test_ai_write_gate>)
    set_tests_properties(test_ai_write_gate PROPERTIES
        LABELS "unit;ai;write-gate;security"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180)
    message(STATUS "Regression test enabled — target: test_ai_write_gate")
endif()

# v0.1.68 — integration test for AI dock auto-exit-fullscreen on tab
# switching. Drives a real MainWindow under offscreen Qt and verifies
# the four user-facing scenarios: openFile, programmatic setCurrentIndex
# (Ctrl+Tab equivalent), Ctrl+N (must STAY fullscreen per v0.1.61 UX),
# and openFile against an already-open path. Needs the full SOURCES
# set because MainWindow's construction graph pulls in everything.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_ai_fullscreen_exit.cpp")
    set(AI_FULLSCREEN_EXIT_SOURCES ${SOURCES})
    list(FILTER AI_FULLSCREEN_EXIT_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_ai_fullscreen_exit
        test_ai_fullscreen_exit.cpp
        ${AI_FULLSCREEN_EXIT_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_ai_fullscreen_exit rust_core)
    target_compile_definitions(test_ai_fullscreen_exit PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_ai_fullscreen_exit PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_ai_fullscreen_exit PRIVATE
        Qt5::Widgets
        Qt5::PrintSupport
        Qt5::Network
        Qt5::Concurrent
        Qt5::Sql
        Qt5::Charts
        ${QSCINTILLA_LIB}
        ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_ai_fullscreen_exit PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_ai_fullscreen_exit PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_ai_fullscreen_exit PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_ai_fullscreen_exit PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_ai_fullscreen_exit PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_ai_fullscreen_exit PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_ai_fullscreen_exit PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_ai_fullscreen_exit PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_ai_fullscreen_exit PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_ai_fullscreen_exit COMMAND $<TARGET_FILE:test_ai_fullscreen_exit>)
    set_tests_properties(test_ai_fullscreen_exit PROPERTIES
        LABELS "integration;ai;fullscreen;v0168"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_ai_fullscreen_exit")
endif()

# ─── Change-history strip (v0.1.91) ─────────────────────────────────────
# Asserts Notepad++-style orange-on-edit / green-on-save behaviour against
# Notepatra's actual Editor class. Reuses the same source set as the
# fullscreen test (it needs everything Editor links to).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_change_history.cpp")
    set(CHANGE_HISTORY_SOURCES ${SOURCES})
    list(FILTER CHANGE_HISTORY_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_change_history
        test_change_history.cpp
        ${CHANGE_HISTORY_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_change_history rust_core)
    target_compile_definitions(test_change_history PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_change_history PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_change_history PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_change_history PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_change_history PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_change_history PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_change_history PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_change_history PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_change_history PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_change_history PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_change_history PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_change_history PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_change_history COMMAND $<TARGET_FILE:test_change_history>)
    set_tests_properties(test_change_history PROPERTIES
        LABELS "integration;editor;changehistory;v0191"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_change_history")
endif()

# Same flow but through MainWindow + TabManager — catches issues where the
# real UI path resets markers that the bare Editor test doesn't see.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_change_history_mainwindow.cpp")
    set(CHANGE_HISTORY_MW_SOURCES ${SOURCES})
    list(FILTER CHANGE_HISTORY_MW_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_change_history_mainwindow
        test_change_history_mainwindow.cpp
        ${CHANGE_HISTORY_MW_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_change_history_mainwindow rust_core)
    target_compile_definitions(test_change_history_mainwindow PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_change_history_mainwindow PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_change_history_mainwindow PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_change_history_mainwindow PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_change_history_mainwindow PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_change_history_mainwindow PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_change_history_mainwindow PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_change_history_mainwindow PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_change_history_mainwindow PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_change_history_mainwindow PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_change_history_mainwindow PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_change_history_mainwindow PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_change_history_mainwindow COMMAND $<TARGET_FILE:test_change_history_mainwindow>)
    set_tests_properties(test_change_history_mainwindow PROPERTIES
        LABELS "integration;mainwindow;changehistory;v0191"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_change_history_mainwindow")
endif()

# ─── restoreSession unreadable-file corruption (v0.1.104) ───────────────
# Asserts that a session whose 2nd tab points at an exists()-but-unreadable
# file does NOT clobber the 1st tab's buffer (the openFile()-appended-no-tab
# aliasing bug). Needs the full SOURCES set because it builds a real
# MainWindow whose ctor runs restoreSession().
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_restore_session_unreadable.cpp")
    set(RESTORE_UNREADABLE_SOURCES ${SOURCES})
    list(FILTER RESTORE_UNREADABLE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_restore_session_unreadable
        test_restore_session_unreadable.cpp
        ${RESTORE_UNREADABLE_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_restore_session_unreadable rust_core)
    target_compile_definitions(test_restore_session_unreadable PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_restore_session_unreadable PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_restore_session_unreadable PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_restore_session_unreadable PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_restore_session_unreadable PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_restore_session_unreadable PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_restore_session_unreadable PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_restore_session_unreadable PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_restore_session_unreadable PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_restore_session_unreadable PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_restore_session_unreadable PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_restore_session_unreadable PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_restore_session_unreadable COMMAND $<TARGET_FILE:test_restore_session_unreadable>)
    set_tests_properties(test_restore_session_unreadable PROPERTIES
        LABELS "integration;mainwindow;session;restore;v0104"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_restore_session_unreadable")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_cold_start_order.cpp")
    set(COLD_START_SOURCES ${SOURCES})
    list(FILTER COLD_START_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_cold_start_order
        test_cold_start_order.cpp
        ${COLD_START_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_cold_start_order rust_core)
    target_compile_definitions(test_cold_start_order PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_cold_start_order PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_cold_start_order PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_cold_start_order PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_cold_start_order PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_cold_start_order PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_cold_start_order PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_cold_start_order PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_cold_start_order PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_cold_start_order PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_cold_start_order PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_cold_start_order PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_cold_start_order COMMAND $<TARGET_FILE:test_cold_start_order>)
    set_tests_properties(test_cold_start_order PROPERTIES
        LABELS "integration;mainwindow;session;startup;coldstart"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_cold_start_order")
endif()

# D2 (win-open-ghost) — remote-open: non-blocking pipe drain + activation
# before deferred file loads. Pure local-socket IPC, fully offline.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_remote_open.cpp")
    set(REMOTE_OPEN_SOURCES ${SOURCES})
    list(FILTER REMOTE_OPEN_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_remote_open
        test_remote_open.cpp
        ${REMOTE_OPEN_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_remote_open rust_core)
    target_compile_definitions(test_remote_open PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_remote_open PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_remote_open PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_remote_open PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_remote_open PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_remote_open PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_remote_open PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_remote_open PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_remote_open PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_remote_open PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_remote_open PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_remote_open PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_remote_open COMMAND $<TARGET_FILE:test_remote_open>)
    set_tests_properties(test_remote_open PROPERTIES
        LABELS "integration;mainwindow;remoteopen;singleinstance"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_remote_open")
endif()

# D3 (win-open-ghost) — status-bar word count: cached + debounced + capped.
# Sections 6/7 are QElapsedTimer ceilings that fail on the old regex path.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_statusbar_wordcount_perf.cpp")
    set(WORDCOUNT_PERF_SOURCES ${SOURCES})
    list(FILTER WORDCOUNT_PERF_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_statusbar_wordcount_perf
        test_statusbar_wordcount_perf.cpp
        ${WORDCOUNT_PERF_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_statusbar_wordcount_perf rust_core)
    target_compile_definitions(test_statusbar_wordcount_perf PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_statusbar_wordcount_perf PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_statusbar_wordcount_perf PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_statusbar_wordcount_perf PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_statusbar_wordcount_perf PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_statusbar_wordcount_perf PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_statusbar_wordcount_perf PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_statusbar_wordcount_perf PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_statusbar_wordcount_perf PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_statusbar_wordcount_perf PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_statusbar_wordcount_perf PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_statusbar_wordcount_perf PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_statusbar_wordcount_perf COMMAND $<TARGET_FILE:test_statusbar_wordcount_perf>)
    set_tests_properties(test_statusbar_wordcount_perf PROPERTIES
        LABELS "integration;statusbar;perf;d3"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_statusbar_wordcount_perf")
endif()

# D4 (win-open-ghost) — bulk setText guard: ScopedBulkLoad + the >1000-line
# threshold keep wholesale loads out of the per-line change-history loop.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_bulk_settext_guard.cpp")
    set(BULK_SETTEXT_SOURCES ${SOURCES})
    list(FILTER BULK_SETTEXT_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_bulk_settext_guard
        test_bulk_settext_guard.cpp
        ${BULK_SETTEXT_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_bulk_settext_guard rust_core)
    target_compile_definitions(test_bulk_settext_guard PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_bulk_settext_guard PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_bulk_settext_guard PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_bulk_settext_guard PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_bulk_settext_guard PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_bulk_settext_guard PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_bulk_settext_guard PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_bulk_settext_guard PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_bulk_settext_guard PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_bulk_settext_guard PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_bulk_settext_guard PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_bulk_settext_guard PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_bulk_settext_guard COMMAND $<TARGET_FILE:test_bulk_settext_guard>)
    set_tests_properties(test_bulk_settext_guard PROPERTIES
        LABELS "integration;editor;changehistory;perf;d4"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_bulk_settext_guard")
endif()

# D5 (win-open-ghost) — pre-show modal hygiene: no exec() before first show;
# watcher prompts coalesce/debounce/defer. Modal drives are guarded.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_preshow_modal_hygiene.cpp")
    set(PRESHOW_MODAL_SOURCES ${SOURCES})
    list(FILTER PRESHOW_MODAL_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_preshow_modal_hygiene
        test_preshow_modal_hygiene.cpp
        ${PRESHOW_MODAL_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_preshow_modal_hygiene rust_core)
    target_compile_definitions(test_preshow_modal_hygiene PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_preshow_modal_hygiene PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_preshow_modal_hygiene PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_preshow_modal_hygiene PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_preshow_modal_hygiene PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_preshow_modal_hygiene PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_preshow_modal_hygiene PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_preshow_modal_hygiene PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_preshow_modal_hygiene PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_preshow_modal_hygiene PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_preshow_modal_hygiene PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_preshow_modal_hygiene PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_preshow_modal_hygiene COMMAND $<TARGET_FILE:test_preshow_modal_hygiene>)
    set_tests_properties(test_preshow_modal_hygiene PROPERTIES
        LABELS "integration;mainwindow;modal;preshow;d5"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_preshow_modal_hygiene")
endif()

# Watcher prompt branches (re-verify fix wave): Yes-reload, File Deleted
# keep/remove, cross-path one-at-a-time deferral, closeTab-modal gate (UAF).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_watcher_prompts.cpp")
    set(WATCHER_PROMPTS_SOURCES ${SOURCES})
    list(FILTER WATCHER_PROMPTS_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_watcher_prompts
        test_watcher_prompts.cpp
        ${WATCHER_PROMPTS_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_watcher_prompts rust_core)
    target_compile_definitions(test_watcher_prompts PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_watcher_prompts PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_watcher_prompts PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_watcher_prompts PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_watcher_prompts PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_watcher_prompts PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_watcher_prompts PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_watcher_prompts PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_watcher_prompts PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_watcher_prompts PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_watcher_prompts PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_watcher_prompts PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_watcher_prompts COMMAND $<TARGET_FILE:test_watcher_prompts>)
    set_tests_properties(test_watcher_prompts PROPERTIES
        LABELS "integration;mainwindow;watcher;modal"
        TIMEOUT 300
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_watcher_prompts")
endif()

# D6 (win-open-ghost) — single-instance ACK protocol + hung-primary fallback.
# Protocol-only: links just singleinstance.cpp, no GUI.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_single_instance.cpp")
    if(NOT TARGET Threads::Threads)
        find_package(Threads REQUIRED)
    endif()
    add_executable(test_single_instance test_single_instance.cpp src/singleinstance.cpp)
    target_include_directories(test_single_instance PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_single_instance PRIVATE Qt5::Core Qt5::Network Threads::Threads)
    notepatra_add_qt_test(test_single_instance)
    set_tests_properties(test_single_instance PROPERTIES
        LABELS "unit;singleinstance;d6" TIMEOUT 120)
    message(STATUS "Regression test enabled — target: test_single_instance")
endif()

# D7 (win-open-ghost) — CLI arg parse contract (the Windows wide-argv fix
# feeds QCoreApplication::arguments() into this parser).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_cli_args.cpp")
    add_executable(test_cli_args test_cli_args.cpp src/cliargs.cpp)
    target_include_directories(test_cli_args PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_cli_args PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_cli_args)
    set_tests_properties(test_cli_args PROPERTIES LABELS "smoke;cli;d7")
    message(STATUS "Regression test enabled — target: test_cli_args")
endif()

# D7 (win-open-ghost) — async-signal-safe crash flag write/read cycle.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_crash_flag.cpp")
    add_executable(test_crash_flag test_crash_flag.cpp src/crashflag.cpp)
    target_include_directories(test_crash_flag PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_crash_flag PRIVATE Qt5::Core)
    notepatra_add_qt_test(test_crash_flag)
    set_tests_properties(test_crash_flag PROPERTIES LABELS "smoke;crash;d7")
    message(STATUS "Regression test enabled — target: test_crash_flag")
endif()

# D6 (win-open-ghost) — standalone/--new windows never read or write
# session or recovery state (hung-primary fallback safety).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_standalone_window.cpp")
    set(STANDALONE_WINDOW_SOURCES ${SOURCES})
    list(FILTER STANDALONE_WINDOW_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_standalone_window
        test_standalone_window.cpp
        ${STANDALONE_WINDOW_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_standalone_window rust_core)
    target_compile_definitions(test_standalone_window PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_standalone_window PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_standalone_window PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_standalone_window PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_standalone_window PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_standalone_window PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_standalone_window PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_standalone_window PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_standalone_window PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_standalone_window PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_standalone_window PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_standalone_window PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_standalone_window COMMAND $<TARGET_FILE:test_standalone_window>)
    set_tests_properties(test_standalone_window PROPERTIES
        LABELS "integration;mainwindow;session;singleinstance;d6"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_standalone_window")
endif()

# D7 (win-open-ghost) — .crash_flag survives MainWindow construction
# (surfaced by main() post-show, not silently wiped); legacy recovery_*
# residue cleaned; dead crash-recovery modal stays dead.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_crash_notice_mainwindow.cpp")
    set(CRASH_NOTICE_SOURCES ${SOURCES})
    list(FILTER CRASH_NOTICE_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_crash_notice_mainwindow
        test_crash_notice_mainwindow.cpp
        ${CRASH_NOTICE_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_crash_notice_mainwindow rust_core)
    target_compile_definitions(test_crash_notice_mainwindow PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_crash_notice_mainwindow PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_crash_notice_mainwindow PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_crash_notice_mainwindow PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_crash_notice_mainwindow PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_crash_notice_mainwindow PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_crash_notice_mainwindow PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_crash_notice_mainwindow PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_crash_notice_mainwindow PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_crash_notice_mainwindow PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_crash_notice_mainwindow PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_crash_notice_mainwindow PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_crash_notice_mainwindow COMMAND $<TARGET_FILE:test_crash_notice_mainwindow>)
    set_tests_properties(test_crash_notice_mainwindow PROPERTIES
        LABELS "integration;mainwindow;crash;d7"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_crash_notice_mainwindow")
endif()

# D8 (win-open-ghost) — session autosave change-detection (idle ticks
# are free), atomic session.json write, .restoring marker PID liveness.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_session_autosave_guard.cpp")
    set(AUTOSAVE_GUARD_SOURCES ${SOURCES})
    list(FILTER AUTOSAVE_GUARD_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_session_autosave_guard
        test_session_autosave_guard.cpp
        ${AUTOSAVE_GUARD_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_session_autosave_guard rust_core)
    target_compile_definitions(test_session_autosave_guard PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_session_autosave_guard PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_session_autosave_guard PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_session_autosave_guard PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_session_autosave_guard PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_session_autosave_guard PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_session_autosave_guard PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_session_autosave_guard PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_session_autosave_guard PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_session_autosave_guard PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_session_autosave_guard PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_session_autosave_guard PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_session_autosave_guard COMMAND $<TARGET_FILE:test_session_autosave_guard>)
    set_tests_properties(test_session_autosave_guard PROPERTIES
        LABELS "integration;mainwindow;session;perf;d8"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 180)
    message(STATUS "Regression test enabled — target: test_session_autosave_guard")
endif()

# D8 (win-open-ghost) — double-click occurrence highlight: doc-size
# gate + match cap so a popular word can't hang the GUI thread.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_occurrence_highlight_cap.cpp")
    set(OCC_CAP_SOURCES ${SOURCES})
    list(FILTER OCC_CAP_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_occurrence_highlight_cap
        test_occurrence_highlight_cap.cpp
        ${OCC_CAP_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_occurrence_highlight_cap rust_core)
    target_compile_definitions(test_occurrence_highlight_cap PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_occurrence_highlight_cap PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_occurrence_highlight_cap PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_occurrence_highlight_cap PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_occurrence_highlight_cap PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_occurrence_highlight_cap PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_occurrence_highlight_cap PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_occurrence_highlight_cap PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_occurrence_highlight_cap PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_occurrence_highlight_cap PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_occurrence_highlight_cap PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_occurrence_highlight_cap PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_occurrence_highlight_cap COMMAND $<TARGET_FILE:test_occurrence_highlight_cap>)
    set_tests_properties(test_occurrence_highlight_cap PROPERTIES
        LABELS "unit;editor;perf;d8"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_occurrence_highlight_cap")
endif()

# Same as test_change_history_mainwindow but pumps events between saves
# so any QFileSystemWatcher race fires inside the test window.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_change_history_watcher.cpp")
    set(CH_WATCHER_SOURCES ${SOURCES})
    list(FILTER CH_WATCHER_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_change_history_watcher
        test_change_history_watcher.cpp
        ${CH_WATCHER_SOURCES}
        ${HEADERS}
    )
    add_dependencies(test_change_history_watcher rust_core)
    target_compile_definitions(test_change_history_watcher PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}"
    )
    target_include_directories(test_change_history_watcher PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include
        ${QSCINTILLA_INCLUDE}
    )
    target_link_libraries(test_change_history_watcher PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB}
    )
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_change_history_watcher PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_change_history_watcher PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_change_history_watcher PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_change_history_watcher PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_change_history_watcher PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_change_history_watcher PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_change_history_watcher PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_change_history_watcher PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_change_history_watcher PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_change_history_watcher COMMAND $<TARGET_FILE:test_change_history_watcher>)
    set_tests_properties(test_change_history_watcher PROPERTIES
        LABELS "integration;mainwindow;changehistory;watcher;v0191"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_change_history_watcher")
endif()

# Stress test — many edit-save cycles to expose state corruption
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_change_history_stress.cpp")
    set(CH_STRESS_SOURCES ${SOURCES})
    list(FILTER CH_STRESS_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_change_history_stress
        test_change_history_stress.cpp ${CH_STRESS_SOURCES} ${HEADERS})
    add_dependencies(test_change_history_stress rust_core)
    target_compile_definitions(test_change_history_stress PRIVATE NOTEPATRA_VERSION="${PROJECT_VERSION}")
    target_include_directories(test_change_history_stress PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_change_history_stress PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB})
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_change_history_stress PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_change_history_stress PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_change_history_stress PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_change_history_stress PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_change_history_stress PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_change_history_stress PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_change_history_stress PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_change_history_stress PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_change_history_stress PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_change_history_stress COMMAND $<TARGET_FILE:test_change_history_stress>)
    set_tests_properties(test_change_history_stress PROPERTIES
        LABELS "integration;mainwindow;changehistory;stress;v0191"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_change_history_stress")
endif()

# ─── v0.1.92 status-bar change-history counter regression ──────────────
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_changehistory_statusbar.cpp")
    set(CH_SB_SOURCES ${SOURCES})
    list(FILTER CH_SB_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_changehistory_statusbar
        test_changehistory_statusbar.cpp ${CH_SB_SOURCES} ${HEADERS})
    add_dependencies(test_changehistory_statusbar rust_core)
    target_compile_definitions(test_changehistory_statusbar PRIVATE NOTEPATRA_VERSION="${PROJECT_VERSION}")
    target_include_directories(test_changehistory_statusbar PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_changehistory_statusbar PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB})
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_changehistory_statusbar PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_changehistory_statusbar PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_changehistory_statusbar PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_changehistory_statusbar PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_changehistory_statusbar PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_changehistory_statusbar PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_changehistory_statusbar PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_changehistory_statusbar PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_changehistory_statusbar PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_changehistory_statusbar COMMAND $<TARGET_FILE:test_changehistory_statusbar>)
    set_tests_properties(test_changehistory_statusbar PROPERTIES
        LABELS "integration;statusbar;changehistory;v0192"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_changehistory_statusbar")
endif()

# ─── Replace correctness: the two bugs that destroyed user text ─────────
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_replace_correctness.cpp")
    set(REPL_CORRECT_SOURCES ${SOURCES})
    list(FILTER REPL_CORRECT_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_replace_correctness
        test_replace_correctness.cpp ${REPL_CORRECT_SOURCES} ${HEADERS})
    add_dependencies(test_replace_correctness rust_core)
    target_compile_definitions(test_replace_correctness PRIVATE NOTEPATRA_VERSION="${PROJECT_VERSION}")
    target_include_directories(test_replace_correctness PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_replace_correctness PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB})
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_replace_correctness PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_replace_correctness PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_replace_correctness PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_replace_correctness PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_replace_correctness PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_replace_correctness PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_replace_correctness PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_replace_correctness PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_replace_correctness PRIVATE ${DUCKDB_LIBRARY})
    endif()
    notepatra_add_qt_test(test_replace_correctness)
    set_tests_properties(test_replace_correctness PROPERTIES
        LABELS "regression;findreplace;dataloss;v01124")
    message(STATUS "Regression test enabled — target: test_replace_correctness")
endif()

# ─── v0.1.92 Find→Replace carry-forward regression ──────────────────────
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_findreplace_carry_forward.cpp")
    set(FR_CARRY_SOURCES ${SOURCES})
    list(FILTER FR_CARRY_SOURCES EXCLUDE REGEX "src/main\\.cpp$")
    add_executable(test_findreplace_carry_forward
        test_findreplace_carry_forward.cpp ${FR_CARRY_SOURCES} ${HEADERS})
    add_dependencies(test_findreplace_carry_forward rust_core)
    target_compile_definitions(test_findreplace_carry_forward PRIVATE NOTEPATRA_VERSION="${PROJECT_VERSION}")
    target_include_directories(test_findreplace_carry_forward PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/charts
        ${CMAKE_SOURCE_DIR}/rust-core/include ${QSCINTILLA_INCLUDE})
    target_link_libraries(test_findreplace_carry_forward PRIVATE
        Qt5::Widgets Qt5::PrintSupport Qt5::Network Qt5::Concurrent Qt5::Sql Qt5::Charts
        ${QSCINTILLA_LIB} ${RUST_LIB})
    if(NOTEPATRA_WITH_WEBENGINE)
        target_compile_definitions(test_findreplace_carry_forward PRIVATE NOTEPATRA_WITH_WEBENGINE=1)
        target_link_libraries(test_findreplace_carry_forward PRIVATE Qt5::WebEngineWidgets)
    endif()
    if(WIN32)
        target_compile_definitions(test_findreplace_carry_forward PRIVATE QSCINTILLA_DLL)
        target_link_libraries(test_findreplace_carry_forward PRIVATE
            ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32 winspool comdlg32)
    elseif(APPLE)
        target_link_libraries(test_findreplace_carry_forward PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_findreplace_carry_forward PRIVATE pthread dl m xcb)
    endif()
    if(NOTEPATRA_USE_DUCKDB AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
        target_compile_definitions(test_findreplace_carry_forward PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
        target_include_directories(test_findreplace_carry_forward PRIVATE ${DUCKDB_INCLUDE_DIR})
        target_link_libraries(test_findreplace_carry_forward PRIVATE ${DUCKDB_LIBRARY})
    endif()
    add_test(NAME test_findreplace_carry_forward COMMAND $<TARGET_FILE:test_findreplace_carry_forward>)
    set_tests_properties(test_findreplace_carry_forward PROPERTIES
        LABELS "integration;findreplace;v0192"
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    message(STATUS "Regression test enabled — target: test_findreplace_carry_forward")
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
        src/ai_interaction_log.cpp
        src/credscrub.cpp
        src/network_policy.cpp   # v0.1.72 — gated in ollama.cpp under NOTEPATRA_NO_CLOUD
    )
    target_include_directories(test_ollama PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_ollama PRIVATE
        Qt5::Core
        Qt5::Network
        Qt5::Sql
    )
    add_test(NAME test_ollama COMMAND $<TARGET_FILE:test_ollama>)
    set_tests_properties(test_ollama PROPERTIES LABELS "integration;ollama")
    message(STATUS "Regression test enabled — target: test_ollama")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_llamacpp.cpp")
    add_executable(test_llamacpp
        test_llamacpp.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
        src/network_policy.cpp   # v0.1.72 — gated in ollama.cpp under NOTEPATRA_NO_CLOUD
    )
    target_include_directories(test_llamacpp PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_llamacpp PRIVATE
        Qt5::Core
        Qt5::Network
        Qt5::Sql
    )
    add_test(NAME test_llamacpp COMMAND $<TARGET_FILE:test_llamacpp>)
    set_tests_properties(test_llamacpp PROPERTIES LABELS "integration;llamacpp;openai-compat")
    message(STATUS "Regression test enabled — target: test_llamacpp")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_aifix.cpp")
    add_executable(test_aifix
        test_aifix.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
        src/network_policy.cpp   # v0.1.72 — gated in ollama.cpp under NOTEPATRA_NO_CLOUD
    )
    target_include_directories(test_aifix PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_aifix PRIVATE
        Qt5::Core
        Qt5::Network
        Qt5::Sql
    )
    add_test(NAME test_aifix COMMAND $<TARGET_FILE:test_aifix>)
    set_tests_properties(test_aifix PROPERTIES LABELS "integration;ollama;aifix")
    message(STATUS "Regression test enabled — target: test_aifix")
endif()

# v0.1.98 — guards the streaming-error fix: a bad cloud API key must surface a
# prompt error(), not hang until the caller's timeout. Network integration
# test; SKIPs (return 0) when offline. See onFinishedOpenAI in src/ollama.cpp.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_stream_error.cpp")
    add_executable(test_stream_error
        test_stream_error.cpp
        src/ollama.cpp
        src/ai_interaction_log.cpp
        src/credscrub.cpp
        src/network_policy.cpp   # v0.1.72 — gated in ollama.cpp under NOTEPATRA_NO_CLOUD
    )
    target_include_directories(test_stream_error PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_stream_error PRIVATE
        Qt5::Core
        Qt5::Network
        Qt5::Sql
    )
    add_test(NAME test_stream_error COMMAND $<TARGET_FILE:test_stream_error>)
    set_tests_properties(test_stream_error PROPERTIES
        LABELS "integration;ollama;openrouter;streaming"
        TIMEOUT 40
    )
    set_property(GLOBAL APPEND PROPERTY notepatra_test_targets test_stream_error)
    message(STATUS "Regression test enabled — target: test_stream_error")
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

# v0.1.72 — network policy test. Covers the by-construction allowlist that
# powers the cloud-free build flavor: every loopback/RFC1918/CGNAT/ULA range
# is private, every cloud LLM endpoint we know about is public.  Always
# built (the helper compiles in regardless of NOTEPATRA_NO_CLOUD — only the
# *use* of it inside aipanel/ollama is gated).
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_network_policy.cpp")
    add_executable(test_network_policy
        test_network_policy.cpp
        src/network_policy.cpp
    )
    target_include_directories(test_network_policy PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_network_policy PRIVATE
        Qt5::Core
        Qt5::Network
    )
    notepatra_add_qt_test(test_network_policy)
    set_tests_properties(test_network_policy PROPERTIES LABELS "unit;security;network-policy;v0172")
    message(STATUS "Security test enabled — target: test_network_policy")
endif()

# v0.1.90 — Vega-Lite translator regression test. Pure-logic: builds
# the chart_spec_to_vega.cpp translator on its own and asserts that
# every supported chart type emits a well-formed Vega-Lite v5 spec,
# multi-metric overlay emits a fold transform, dual-axis emits a 2-
# layer spec with independent y scales, and the facet wrapper composes
# with all of the above.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_chart_spec_to_vega.cpp")
    add_executable(test_chart_spec_to_vega
        test_chart_spec_to_vega.cpp
        src/chart_spec_to_vega.cpp
    )
    target_include_directories(test_chart_spec_to_vega PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_chart_spec_to_vega PRIVATE Qt5::Core)
    add_test(NAME test_chart_spec_to_vega COMMAND $<TARGET_FILE:test_chart_spec_to_vega>)
    set_tests_properties(test_chart_spec_to_vega PROPERTIES LABELS "unit;charts;v0190")
    message(STATUS "Vega translator test enabled — target: test_chart_spec_to_vega")
endif()

# v0.1.76 — expanded chart-type catalogue regression test. Validates
# that every new type in src/chartrender.cpp (area, horizontal-bar,
# stacked-bar, stacked-horizontal-bar, grouped-bar, donut, histogram,
# boxplot) renders without error.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_chart_types.cpp")
    add_executable(test_chart_types
        test_chart_types.cpp
        src/chartrender.cpp
        src/chart_modal.cpp
        src/chart_spec_to_vega.cpp
    )
    target_include_directories(test_chart_types PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_chart_types PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Charts
    )
    notepatra_add_qt_test(test_chart_types)
    set_tests_properties(test_chart_types PROPERTIES LABELS "unit;charts;v0176")
    message(STATUS "Chart-types test enabled — target: test_chart_types")
endif()

# v0.1.75 — font-pack manifest + scan-and-load regression test. Builds
# only fontpack.cpp + its deps; no UI tree linkage.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_fontpack.cpp")
    add_executable(test_fontpack
        test_fontpack.cpp
        src/fontpack.cpp
    )
    target_include_directories(test_fontpack PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(test_fontpack PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Network
    )
    notepatra_add_qt_test(test_fontpack)
    set_tests_properties(test_fontpack PROPERTIES LABELS "unit;fontpack;v0175")
    message(STATUS "Font-pack test enabled — target: test_fontpack")
endif()

# ── GitPanel wiring test (v0.1.48 inline diff + widget tree) ──
# Offscreen-safe: constructs the panel, asserts the splitter/diff-view
# plumbing and diff line colours. Never opens a repo, never hits a modal.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_gitpanel.cpp")
    add_executable(test_gitpanel
        test_gitpanel.cpp
        src/gitpanel.cpp
    )
    target_include_directories(test_gitpanel PRIVATE ${CMAKE_SOURCE_DIR})
    target_link_libraries(test_gitpanel PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets)
    notepatra_add_qt_test(test_gitpanel)
    set_tests_properties(test_gitpanel PROPERTIES LABELS "unit;git;panel")
    message(STATUS "Regression test enabled — target: test_gitpanel")
endif()

# ── MCP bridge protocol test (v0.1.118) ──
# Offscreen-safe: fake editor host + real QLocalServer round-trip. Links
# Widgets because the write tier's human-approval card is a real QFrame the
# test clicks programmatically. singleinstance.cpp is only for the
# server-name derivation; the handshake is covered by test_single_instance.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_mcp_bridge.cpp")
    add_executable(test_mcp_bridge
        test_mcp_bridge.cpp
        src/mcp_bridge.cpp
        src/singleinstance.cpp
        # v0.1.118 — list_notes/read_note go through the real NotesStorage
        # layer (QtCore-only; notes_template.cpp resolves shellHtml).
        src/notes_storage.cpp
        src/notes_template.cpp
        # v0.1.119 — validate_npd calls the REAL parse-only entry point
        # Npd::parse (pure QtCore, no widgets).
        src/diagram/npd_parser.cpp
        # phase 2 — run_query/list_tables exercise the REAL classifier + QSQLITE
        # runQuery (compiled with NO NOTEPATRA_HAVE_DUCKDB, so no DuckDB link).
        src/dbconnections.cpp
    )
    target_include_directories(test_mcp_bridge PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(test_mcp_bridge PRIVATE
        NOTEPATRA_VERSION="${PROJECT_VERSION}")
    target_link_libraries(test_mcp_bridge PRIVATE
        Qt5::Core Qt5::Gui Qt5::Widgets Qt5::Network Qt5::Concurrent
        Qt5::Sql Qt5::Test)
    notepatra_add_qt_test(test_mcp_bridge)
    set_tests_properties(test_mcp_bridge PROPERTIES LABELS "unit;mcp;ipc")
    message(STATUS "Regression test enabled — target: test_mcp_bridge")
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

# v0.1.111 — Myers inline-edit diff mapping. Asserts that a shifted insert
# highlights exactly the inserted line (the naive line-index diff's failure
# mode) and that the trailing-newline off-by-one between Qt split and Rust
# from_lines is handled without crashing.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_inline_diff.cpp")
    add_executable(test_inline_diff
        test_inline_diff.cpp
        src/inlineedit_diff.cpp
        src/rustbridge.cpp
    )
    add_dependencies(test_inline_diff rust_core)
    target_include_directories(test_inline_diff PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/rust-core/include
    )
    target_link_libraries(test_inline_diff PRIVATE
        Qt5::Core
        ${RUST_LIB}
    )
    if(WIN32)
        target_link_libraries(test_inline_diff PRIVATE
            ws2_32 userenv bcrypt ntdll
            advapi32 ole32 shell32 dbghelp
            kernel32 user32 gdi32
        )
    elseif(APPLE)
        target_link_libraries(test_inline_diff PRIVATE pthread dl m
            "-framework Security" "-framework CoreFoundation")
    else()
        target_link_libraries(test_inline_diff PRIVATE pthread dl m)
    endif()
    notepatra_add_qt_test(test_inline_diff)
    set_tests_properties(test_inline_diff PROPERTIES LABELS "smoke;diff;ui")
    message(STATUS "Smoke test enabled — target: test_inline_diff")
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

# DB query cancel test — the SHIPPING interrupt path: DbConnections::runQuery
# threaded with a QueryInterruptToken so a controller thread aborts a running
# DuckDB query mid-flight (duckdb_interrupt), plus the DuckDb engine-level
# read-only contract, the row-cap, and the SQL classifier port. DuckDB-gated
# exactly like the test_duckdb smoke test above; links the real query path deps.
if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_db_query_runner.cpp"
   AND DUCKDB_INCLUDE_DIR AND DUCKDB_LIBRARY)
    add_executable(test_db_query_runner
        test_db_query_runner.cpp
        src/dbconnections.cpp
        src/duckdb_client.cpp
    )
    target_include_directories(test_db_query_runner PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${DUCKDB_INCLUDE_DIR}
    )
    target_compile_definitions(test_db_query_runner PRIVATE NOTEPATRA_HAVE_DUCKDB=1)
    target_link_libraries(test_db_query_runner PRIVATE
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Sql
        ${DUCKDB_LIBRARY}
    )
    if(UNIX AND NOT APPLE)
        set_target_properties(test_db_query_runner PROPERTIES
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/vendor/duckdb"
        )
    elseif(APPLE)
        set_target_properties(test_db_query_runner PROPERTIES
            BUILD_RPATH "${CMAKE_SOURCE_DIR}/vendor/duckdb"
        )
    endif()
    # notepatra_add_qt_test → runs under QT_QPA_PLATFORM=offscreen and adds the
    # target to the notepatra_all_tests meta-target so CI always builds it.
    notepatra_add_qt_test(test_db_query_runner)
    # ALWAYS set a TIMEOUT (enable_testing() has no default; the heavy cancel/
    # timeout sub-tests each self-abort at a 25s failsafe in the red state).
    set_tests_properties(test_db_query_runner PROPERTIES
        LABELS "unit;duckdb;data;async" TIMEOUT 180)
    message(STATUS "DB query cancel test enabled — target: test_db_query_runner")
endif()

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_fmtpanel_diff.cpp")
    add_executable(test_fmtpanel_diff
        test_fmtpanel_diff.cpp
        src/fmtpanel.cpp
        src/npp_palette.cpp
        # EditorSymbols::applyFromConfig() is header-only, but apply() itself
        # is not — these panels draw Show Symbol on their own Scintilla views.
        src/editor_symbols.cpp
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
        # EditorSymbols::applyFromConfig() is header-only, but apply() itself
        # is not — these panels draw Show Symbol on their own Scintilla views.
        src/editor_symbols.cpp
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
        src/lexer_plaintext.cpp
        src/lexer_csv.cpp
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
        src/editor_symbols.cpp   # Editor::applySymbolSettings() links against it
        src/gitgutter.cpp
        src/git_hunk_apply.cpp
        src/gutter_hunk_popup.cpp
        src/diff_view.cpp
        src/lexerutils.cpp
        src/network_policy.cpp
        src/npp_palette.cpp
        src/rustbridge.cpp
        src/lexer_go.cpp
        src/lexer_kotlin.cpp
        src/lexer_powershell.cpp
        src/lexer_rust.cpp
        src/lexer_swift.cpp
        src/lexer_typescript.cpp
        src/lexer_extras.cpp
        src/lexer_plaintext.cpp
        src/lexer_csv.cpp
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
        Qt5::Network
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

# v0.1.114's second tag run: a test with no TIMEOUT hung macOS CI for 2 hours
# (plain enable_testing() has no default — a wedged test waits for the 6-hour
# job kill). Every test gets a ceiling; explicit per-test TIMEOUTs win.
if(NOTEPATRA_BUILD_TESTS)
    get_property(_all_tests DIRECTORY "${CMAKE_SOURCE_DIR}" PROPERTY TESTS)
    foreach(_t IN LISTS _all_tests)
        get_test_property(${_t} TIMEOUT _to)
        if(NOT _to)
            set_tests_properties(${_t} PROPERTIES TIMEOUT 600)
        endif()
    endforeach()
endif()
