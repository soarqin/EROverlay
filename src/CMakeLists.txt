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

    # plugin functions
    plugin.cpp plugin.hpp

    # Embedded font
    embeddedfont.cpp

    # Plugin API
    api/api.cpp api/api.h
    api/define.h

    # util
    util/file.cpp util/file.hpp
    util/memory.hpp util/signature.hpp
    util/steam.cpp util/steam.hpp
    util/sysfont.cpp util/sysfont.hpp
    util/string.hpp util/vector.hpp
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC _CRT_SECURE_NO_WARNINGS PRIVATE EROVERLAY_EXPORTS)
endif()
if(NOT MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC -mavx2 -mlzcnt -mbmi -mbmi2)
endif()
target_include_directories(${PROJECT_NAME} PUBLIC .)
target_link_libraries(${PROJECT_NAME} PUBLIC Pattern16 imgui fmt::fmt PRIVATE nlohmann_json::nlohmann_json inih steam_api64 imgui_dx12_backend minhook d3dcompiler dwmapi shlwapi version)
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE Microsoft::DirectXMath)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>"
    ARCHIVE_OUTPUT_NAME "EROverlay"
    RUNTIME_OUTPUT_NAME "EROverlay")

add_subdirectory(bosses)
add_subdirectory(achievements)
