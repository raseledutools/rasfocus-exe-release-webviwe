cmake_minimum_required(VERSION 3.20)
project(RasBrowser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─────────────────────────────────────────────────────────────────────────────
# WebView2 SDK Path
# TODO: NuGet থেকে WebView2 download করো, নিচের path ঠিক করো
# Download: https://www.nuget.org/packages/Microsoft.Web.WebView2
# ─────────────────────────────────────────────────────────────────────────────
set(WEBVIEW2_SDK_PATH "C:/path/to/WebView2" CACHE PATH "WebView2 SDK root")

# ─────────────────────────────────────────────────────────────────────────────
# Source Files
# ─────────────────────────────────────────────────────────────────────────────
set(SOURCES
    mini_browser.cpp
    browser_profiles.cpp
    advanced_feature.cpp
    feature_browser.cpp
    bookmarks.cpp
    settings.cpp
    find_in_page.cpp
    context_menu.cpp
    history_panel.cpp
    downloads_panel.cpp
    resource.rc
)

set(HEADERS
    mini_browser.h
    browser_profiles.h
    advanced_feature.h
    feature_browser.h
    html_tools.h
    bookmarks.h
    settings.h
    find_in_page.h
    context_menu.h
    history_panel.h
    downloads_panel.h
)

# ─────────────────────────────────────────────────────────────────────────────
# Executable (WIN32 = no console window)
# ─────────────────────────────────────────────────────────────────────────────
add_executable(RasBrowser WIN32 ${SOURCES} ${HEADERS})

# ─────────────────────────────────────────────────────────────────────────────
# WebView2 Include & Library
# ─────────────────────────────────────────────────────────────────────────────
target_include_directories(RasBrowser PRIVATE
    ${WEBVIEW2_SDK_PATH}/build/native/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(RasBrowser PRIVATE
    ${WEBVIEW2_SDK_PATH}/build/native/x64/WebView2LoaderStatic.lib
)

# ─────────────────────────────────────────────────────────────────────────────
# Windows System Libraries
# ─────────────────────────────────────────────────────────────────────────────
target_link_libraries(RasBrowser PRIVATE
    comctl32
    dwmapi
    uxtheme
    gdiplus
    shlwapi
    shell32
    ole32
    user32
    gdi32
)

# ─────────────────────────────────────────────────────────────────────────────
# Compiler Options
# ─────────────────────────────────────────────────────────────────────────────
target_compile_definitions(RasBrowser PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
    GDIPVER=0x0110
)

if(MSVC)
    target_compile_options(RasBrowser PRIVATE
        /W3         # Warning level 3
        /MP         # Multi-processor compilation
        /utf-8      # UTF-8 source
    )
    # Subsystem: Windows (no console)
    set_target_properties(RasBrowser PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Post-build: WebView2Loader.dll copy করো
# ─────────────────────────────────────────────────────────────────────────────
add_custom_command(TARGET RasBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${WEBVIEW2_SDK_PATH}/runtimes/win-x64/native/WebView2Loader.dll"
        "$<TARGET_FILE_DIR:RasBrowser>/WebView2Loader.dll"
    COMMENT "Copying WebView2Loader.dll..."
)

# ─────────────────────────────────────────────────────────────────────────────
# BUILD INSTRUCTIONS (README)
# ─────────────────────────────────────────────────────────────────────────────
# 1. Visual Studio 2022 install করো (MSVC compiler সহ)
# 2. CMake install করো (https://cmake.org)
# 3. WebView2 SDK download করো:
#    https://www.nuget.org/packages/Microsoft.Web.WebView2
#    unzip করো, WEBVIEW2_SDK_PATH set করো
# 4. Build করো:
#    mkdir build && cd build
#    cmake .. -DWEBVIEW2_SDK_PATH="C:/WebView2SDK"
#    cmake --build . --config Release
# ─────────────────────────────────────────────────────────────────────────────
