cmake_minimum_required(VERSION 4.0)
project(Tray)

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

add_executable(tray)
target_compile_options(tray PUBLIC -std=c++26)
target_sources(tray PUBLIC main.cpp)
target_link_libraries(tray PUBLIC SDL3 imgui)

install(TARGETS tray RUNTIME DESTINATION bin)
