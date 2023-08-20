project(EROverlayDLL CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(DirectXMath)

add_library(${PROJECT_NAME} SHARED dllmain.cpp
    initialize.hpp
    console.cpp console.hpp
    d3drenderer.cpp d3drenderer.hpp
    hooking.cpp hooking.hpp
    global.cpp global.hpp
    renderbase.hpp
    bosses/data.cpp bosses/data.hpp bosses/render.cpp bosses/render.hpp
)
target_link_libraries(${PROJECT_NAME} minhook imgui_dx12_backend nlohmann_json::nlohmann_json Microsoft::DirectXMath d3dcompiler dwmapi shlwapi)
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_NAME "EROverlay"
    RUNTIME_OUTPUT_NAME "EROverlay")
