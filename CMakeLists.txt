cmake_minimum_required(VERSION 3.16)
project(NotepadPlusPlusLinux VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# ── Qt5 ──
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network)

# ── QScintilla ──
find_library(QSCINTILLA_LIB qscintilla2_qt5 REQUIRED)
find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
    PATHS /usr/include /usr/include/x86_64-linux-gnu/qt5
    PATH_SUFFIXES Qsci)

# ── Rust core library ──
set(RUST_CORE_DIR ${CMAKE_SOURCE_DIR}/rust-core)
set(RUST_LIB ${RUST_CORE_DIR}/target/release/libnotepad_core.a)

# Build Rust library as a custom command
add_custom_command(
    OUTPUT ${RUST_LIB}
    COMMAND cargo build --release
    WORKING_DIRECTORY ${RUST_CORE_DIR}
    COMMENT "Building Rust core library..."
)
add_custom_target(rust_core DEPENDS ${RUST_LIB})

# ── Sources ──
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/editor.cpp
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
    src/aipanel.cpp
    src/restclient.cpp
    src/gitpanel.cpp
    src/sqlfmtpanel.cpp
    src/fmtpanel.cpp
    src/searchresults.cpp
)

set(HEADERS
    src/mainwindow.h
    src/editor.h
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
    src/ollamastatus.h
    src/themes.h
    src/config.h
)

# ── Executable ──
add_executable(notepad-pp-linux ${SOURCES} ${HEADERS})
add_dependencies(notepad-pp-linux rust_core)

target_include_directories(notepad-pp-linux PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/rust-core/include
    ${QSCINTILLA_INCLUDE}/..
)

target_link_libraries(notepad-pp-linux PRIVATE
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Network
    ${QSCINTILLA_LIB}
    ${RUST_LIB}
    pthread dl m
)

# ── Install ──
install(TARGETS notepad-pp-linux DESTINATION bin)
