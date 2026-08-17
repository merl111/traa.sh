cmake_minimum_required(VERSION 3.16)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
project(traash C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TRAASH_BUILD_TESTS "Build tests" ON)

include(FetchContent)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(FREETYPE REQUIRED freetype2)
pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
pkg_check_modules(LUA lua)
if(NOT LUA_FOUND)
  pkg_check_modules(LUA lua5.4)
endif()
if(NOT LUA_FOUND)
  pkg_check_modules(LUA lua-5.4)
endif()
if(NOT LUA_FOUND)
  message(FATAL_ERROR "Lua not found (tried pkg-config names: lua, lua5.4, lua-5.4)")
endif()

if(NOT APPLE)
  find_library(MATH_LIB m REQUIRED)
endif()
find_library(UTIL_LIB util)

if(APPLE)
  set(TRAASH_INSTALL_RPATH "@executable_path/../lib")
else()
  set(TRAASH_INSTALL_RPATH "$ORIGIN/../lib")
endif()

set(TRAASH_CORE_SOURCES
  src/util/log.c
  src/util/utf8.c
  src/util/ring.c
  src/util/path.c
  src/term/cell.c
  src/term/screen.c
  src/term/vt.c
  src/term/pty.c
  src/term/selection.c
  src/mux/pane.c
  src/mux/window.c
  src/mux/session.c
  src/mux/server.c
  src/mux/ipc.c
  src/mux/layout.c
  src/mux/layout_store.c
  src/input/keymap.c
  src/input/actions.c
  src/config/config.c
  src/config/theme.c
  src/config/status.c
  src/plugin/host.c
  src/demo/demo.c
)

add_library(traash_core STATIC ${TRAASH_CORE_SOURCES})
target_include_directories(traash_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${LUA_INCLUDE_DIRS}
  ${FREETYPE_INCLUDE_DIRS}
  ${HARFBUZZ_INCLUDE_DIRS}
  ${FONTCONFIG_INCLUDE_DIRS}
)
target_compile_options(traash_core PUBLIC
  ${LUA_CFLAGS_OTHER}
  ${FREETYPE_CFLAGS_OTHER}
  ${HARFBUZZ_CFLAGS_OTHER}
  ${FONTCONFIG_CFLAGS_OTHER}
  -Wall -Wextra -Wno-unused-parameter
)
target_link_directories(traash_core PUBLIC
  ${LUA_LIBRARY_DIRS}
  ${FREETYPE_LIBRARY_DIRS}
  ${HARFBUZZ_LIBRARY_DIRS}
  ${FONTCONFIG_LIBRARY_DIRS}
)
target_link_libraries(traash_core PUBLIC
  ${LUA_LIBRARIES}
  ${FREETYPE_LIBRARIES}
  ${HARFBUZZ_LIBRARIES}
  ${FONTCONFIG_LIBRARIES}
  Threads::Threads
)
if(MATH_LIB)
  target_link_libraries(traash_core PUBLIC ${MATH_LIB})
endif()
if(UTIL_LIB)
  target_link_libraries(traash_core PUBLIC ${UTIL_LIB})
endif()
target_compile_definitions(traash_core PUBLIC
  TRAASH_LUA_DIR="${CMAKE_INSTALL_PREFIX}/share/traash/lua"
  TRAASH_SOURCE_LUA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lua"
)

set(TRAASH_APP_SOURCES
  src/main.c
  src/app/app.c
  src/app/cli.c
  src/platform/platform_posix.c
  src/gfx/gl_loader.c
  src/gfx/renderer.c
  src/gfx/atlas.c
  src/font/font.c
  src/assets/icon_embedded.c
  src/ui/chrome.c
  src/ui/palette.c
  src/ui/status_bar.c
  src/ui/context_menu.c
  src/ui/shortcuts_overlay.c
  src/ui/layout_overlay.c
  src/ui/overview_overlay.c
  src/ui/quit_confirm.c
  src/ui/settings.c
  src/ui/settings_window.c
  src/ui/nk_impl.c
)

add_executable(traash ${TRAASH_APP_SOURCES})
set_target_properties(traash PROPERTIES
  INSTALL_RPATH "${TRAASH_INSTALL_RPATH}"
  BUILD_WITH_INSTALL_RPATH FALSE
)
target_include_directories(traash PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(traash PRIVATE
  traash_core
  glfw
  OpenGL::GL
)

# Install Lua assets next to build for dev runs
add_custom_command(TARGET traash POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/lua $<TARGET_FILE_DIR:traash>/lua
  COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:traash>/assets/icons
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/assets/icons/traash.png
    $<TARGET_FILE_DIR:traash>/assets/icons/traash.png
)

install(TARGETS traash RUNTIME DESTINATION bin)
install(DIRECTORY lua/ DESTINATION share/traash/lua)
install(DIRECTORY assets/ DESTINATION share/traash/assets)
if(UNIX AND NOT APPLE)
  install(FILES assets/linux/sh.traa.traash.desktop DESTINATION share/applications)
  # Freedesktop icon theme (Wayland / app launchers use this; X11 also gets glfwSetWindowIcon)
  foreach(sz 16 32 48 64 128 256 512)
    install(FILES assets/icons/hicolor/${sz}x${sz}/apps/sh.traa.traash.png
            DESTINATION share/icons/hicolor/${sz}x${sz}/apps)
  endforeach()
endif()

if(TRAASH_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
