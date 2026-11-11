# FlashShot.exe, its core library and the tests. Use build.ps1, which also
# fetches the build tools and builds the installer.
cmake_minimum_required(VERSION 3.21)
project(FlashShot CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /WX /permissive- /sdl /utf-8 /guard:cf /external:anglebrackets /external:W0)
add_link_options(/guard:cf /CETCOMPAT /OPT:REF /OPT:ICF)

# Annotations, editing session, composition and export: no windows involved.
add_library(flashshotcore STATIC
    src/core/annotation.cpp
    src/core/clipboard.cpp
    src/core/encode.cpp
    src/core/geometry.cpp
    src/core/hotkey.cpp
    src/core/image.cpp
    src/core/paths.cpp
    src/core/render.cpp
    src/core/session.cpp
    src/core/settings.cpp
)
target_include_directories(flashshotcore PUBLIC src)
target_link_libraries(flashshotcore PUBLIC user32 advapi32 shell32 ole32 oleaut32 gdi32 gdiplus windowscodecs)

add_executable(FlashShot WIN32
    src/app/capture_overlay.cpp
    src/app/home_window.cpp
    src/app/main.cpp
    src/app/screen_capture.cpp
    src/app/settings_dialog.cpp
    src/app/theme.cpp
    src/app/toast.cpp
    src/app/toolbar.cpp
    src/app/tray_app.cpp
    src/app/FlashShot.rc
)
target_link_libraries(FlashShot PRIVATE flashshotcore comctl32 comdlg32 dwmapi uxtheme shcore)
# rc.exe does not track its includes, so the version number would stay stale.
set_source_files_properties(src/app/FlashShot.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/src/version.h;${CMAKE_SOURCE_DIR}/assets/flashshot.ico")
# The manifest is embedded through FlashShot.rc.
target_link_options(FlashShot PRIVATE /MANIFEST:NO)

add_executable(FlashShotTests tests/tests.cpp src/app/screen_capture.cpp)
target_link_libraries(FlashShotTests PRIVATE flashshotcore shcore)
