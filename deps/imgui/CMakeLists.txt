project(imgui_dx12 CXX)

set(SOURCES_IMGUI 
  "imgui.cpp"
  "imgui_draw.cpp"
  "imgui_tables.cpp"
  "imgui_widgets.cpp"
)
set(SOURCES_BACKEND
  "imgui_impl_dx12.cpp"
  "imgui_impl_win32.cpp"
)

add_library(imgui STATIC ${SOURCES_IMGUI})
add_library(imgui_dx12_backend STATIC ${SOURCES_BACKEND})

target_include_directories(imgui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(imgui_dx12_backend PUBLIC imgui)
