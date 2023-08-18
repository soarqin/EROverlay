project(EROverlayDLL CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(DirectXMath)

add_library(${PROJECT_NAME} SHARED dllmain.cpp console.cpp d3drenderer.cpp hooking.cpp)
target_link_libraries(${PROJECT_NAME} minhook imgui_dx12_backend Microsoft::DirectXMath D3DCompiler Dwmapi)
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_NAME "EROverlay"
    RUNTIME_OUTPUT_NAME "EROverlay")
