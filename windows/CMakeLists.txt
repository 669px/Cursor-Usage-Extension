cmake_minimum_required(VERSION 3.16)
project(CursorUsageWidget LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(CursorUsage WIN32
  src/main.cpp
  src/util.cpp
  src/config.cpp
  src/auth.cpp
  src/usage.cpp
  src/widget.cpp
)

target_include_directories(CursorUsage PRIVATE src)

target_compile_definitions(CursorUsage PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
)

target_link_libraries(CursorUsage PRIVATE
  winhttp
  gdiplus
  shell32
  ole32
  comctl32
  dwmapi
  uxtheme
  gdi32
  user32
)

if(MSVC)
  target_compile_options(CursorUsage PRIVATE /W4 /permissive-)
  target_link_options(CursorUsage PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/app.manifest"
  )
elseif(MINGW)
  target_compile_options(CursorUsage PRIVATE -Wall -Wextra)
  target_link_options(CursorUsage PRIVATE
    -static
    -static-libgcc
    -static-libstdc++
    -mwindows
    -municode
  )
endif()

install(TARGETS CursorUsage RUNTIME DESTINATION bin)