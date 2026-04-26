cmake_minimum_required(VERSION 3.16)
project(Notepatra VERSION 0.1.29 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
option(BUILD_TESTING "Build Notepatra regression tests" OFF)
option(BUILD_LEXER_TEST "Backward-compatible alias for BUILD_TESTING" OFF)

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
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network Concurrent)

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
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/editor.cpp
    src/lexerutils.cpp
    src/npp_palette.cpp
    src/tabmanager.cpp
    src/statusbar.cpp
    src/findreplace.cpp
    src/fileexplorer.cpp
    src/functionlist.cpp
    src/preferences.cpp
    src/rustbridge.cpp
    src/compare.cpp
    src/plugin.cpp
    src/terminal.cpp
    src/markdownpreview.cpp
    src/hexeditor.cpp
    src/gitgutter.cpp
    src/ollama.cpp
    src/ai_context.cpp
    src/ai_systemprompt.cpp
    src/lexer_go.cpp
    src/lexer_kotlin.cpp
    src/lexer_powershell.cpp
    src/lexer_rust.cpp
    src/lexer_swift.cpp
    src/lexer_typescript.cpp
    src/aipanel.cpp
    src/restclient.cpp
    src/gitpanel.cpp
    src/sqlfmtpanel.cpp
    src/fmtpanel.cpp
    src/searchresults.cpp
    src/welcome.cpp
    src/projectsearch.cpp
    src/updater.cpp
)

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
    ${QSCINTILLA_LIB}
    ${RUST_LIB}
)

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

# ── Regression tests (opt in via -DBUILD_TESTING=ON) ──
# BUILD_LEXER_TEST remains as a compatibility alias for old scripts/docs.
function(notepatra_add_qt_test target_name)
    add_test(
        NAME ${target_name}
        COMMAND ${CMAKE_COMMAND} -E env QT_QPA_PLATFORM=offscreen $<TARGET_FILE:${target_name}>
    )
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

if(NOTEPATRA_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/test_palette.cpp")
    add_executable(test_palette
        test_palette.cpp
        src/npp_palette.cpp
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
