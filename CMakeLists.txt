cmake_minimum_required(VERSION 3.16)
project(Notepatra VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

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
find_package(Qt5 REQUIRED COMPONENTS Widgets PrintSupport Network)

# ── QScintilla ──
find_library(QSCINTILLA_LIB qscintilla2_qt5 REQUIRED)
if(WIN32)
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATH_SUFFIXES Qsci)
elseif(APPLE)
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATHS /usr/local/include /opt/homebrew/include
        PATH_SUFFIXES Qsci)
else()
    find_path(QSCINTILLA_INCLUDE Qsci/qsciscintilla.h
        PATHS /usr/include /usr/include/x86_64-linux-gnu/qt5
        PATH_SUFFIXES Qsci)
endif()

# ── Rust core library ──
set(RUST_CORE_DIR ${CMAKE_SOURCE_DIR}/rust-core)
if(WIN32)
    set(RUST_LIB ${RUST_CORE_DIR}/target/release/notepad_core.lib)
else()
    set(RUST_LIB ${RUST_CORE_DIR}/target/release/libnotepad_core.a)
endif()

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

# ── Platform-specific resources ──
if(WIN32)
    # Windows: embed icon via .rc resource file
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/notepatra.rc")
        list(APPEND SOURCES resources/notepatra.rc)
    endif()
endif()

# ── Executable ──
if(APPLE)
    # macOS: build as an app bundle
    add_executable(${EXE_NAME} MACOSX_BUNDLE ${SOURCES} ${HEADERS})
    set_target_properties(${EXE_NAME} PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Notepatra"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.notepatra.editor"
    )
    # Embed icon if present
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/notepatra.icns")
        set_source_files_properties(resources/notepatra.icns PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(${EXE_NAME} PRIVATE resources/notepatra.icns)
        set_target_properties(${EXE_NAME} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "notepatra.icns")
    endif()
elseif(WIN32)
    # Windows: WIN32 flag suppresses console window
    add_executable(${EXE_NAME} WIN32 ${SOURCES} ${HEADERS})
else()
    # Linux: plain executable
    add_executable(${EXE_NAME} ${SOURCES} ${HEADERS})
endif()

add_dependencies(${EXE_NAME} rust_core)

target_include_directories(${EXE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/rust-core/include
    ${QSCINTILLA_INCLUDE}/..
)

# ── Linking ──
target_link_libraries(${EXE_NAME} PRIVATE
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Network
    ${QSCINTILLA_LIB}
    ${RUST_LIB}
)

if(WIN32)
    target_link_libraries(${EXE_NAME} PRIVATE ws2_32 userenv bcrypt ntdll)
elseif(APPLE)
    target_link_libraries(${EXE_NAME} PRIVATE pthread dl m
        "-framework Security" "-framework CoreFoundation")
else()
    target_link_libraries(${EXE_NAME} PRIVATE pthread dl m)
endif()

# ── Install ──
if(APPLE)
    install(TARGETS ${EXE_NAME} BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS ${EXE_NAME} RUNTIME DESTINATION bin)
else()
    install(TARGETS ${EXE_NAME} DESTINATION bin)
endif()
