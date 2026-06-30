cmake_minimum_required(VERSION 4.0)
project(Tray)

# ------------------------------------------------------------------------------

add_library(imgui)
target_include_directories(imgui PUBLIC imgui)
target_sources(imgui PRIVATE
  imgui/imgui.cpp
  imgui/imgui_demo.cpp
  imgui/imgui_draw.cpp
  imgui/imgui_tables.cpp
  imgui/imgui_widgets.cpp
  imgui/misc/cpp/imgui_stdlib.cpp
  imgui/backends/imgui_impl_sdl3.cpp
  imgui/backends/imgui_impl_sdlrenderer3.cpp
  )

# ------------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3       REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(gtk        REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(gdk-pixbuf REQUIRED IMPORTED_TARGET gdk-pixbuf-2.0)
pkg_check_modules(libdbus    REQUIRED IMPORTED_TARGET dbus-1)

# ------------------------------------------------------------------------------

add_executable(tray-service)
target_compile_options(tray-service PUBLIC -std=c++26 -freflection)
target_sources(tray-service PUBLIC src/service.cpp)
target_link_libraries(tray-service PUBLIC PkgConfig::libdbus stdc++exp)

# ------------------------------------------------------------------------------

add_executable(tray)
target_compile_options(tray PUBLIC -std=c++26 -freflection)
target_sources(tray PUBLIC src/tray.cpp)
target_link_libraries(tray PUBLIC
    PkgConfig::SDL3
    PkgConfig::gtk
    PkgConfig::gdk-pixbuf
    PkgConfig::libdbus
    imgui
    stdc++exp
    )

# ------------------------------------------------------------------------------

install(TARGETS tray-service tray RUNTIME DESTINATION bin)
