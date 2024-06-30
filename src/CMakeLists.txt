project(EROverlayDLL CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    find_package(DirectXMath REQUIRED)
endif()

add_library(${PROJECT_NAME} SHARED
    # main files
    dllmain.cpp
    config.cpp config.hpp
    d3drenderer.cpp d3drenderer.hpp
    hooking.cpp hooking.hpp
    global.cpp global.hpp
    renderbase.hpp

    # SteamAPI usage
    steamapi.cpp steamapi.hpp

    # util
    util/vector.hpp

    # Bosses panel
    bosses/data.cpp bosses/data.hpp
    bosses/render.cpp bosses/render.hpp
)
target_compile_definitions(${PROJECT_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
if(NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -mavx2 -mlzcnt -mbmi -mbmi2)
endif()
target_link_libraries(${PROJECT_NAME} Pattern16 inih minhook imgui_dx12_backend nlohmann_json::nlohmann_json d3dcompiler dwmapi shlwapi)
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} Microsoft::DirectXMath)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_NAME "EROverlay"
    RUNTIME_OUTPUT_NAME "EROverlay")
