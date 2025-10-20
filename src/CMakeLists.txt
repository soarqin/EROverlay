add_project(EROverlayDLL SHARED
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
    api.cpp api.h

    # params
    params/param.cpp params/param.hpp
    params/paraminternal.hpp
    params/pointers.cpp params/pointers.hpp
    params/wstring.cpp params/wstring.hpp

    # util
    util/file.cpp util/file.hpp
    util/memory.hpp util/signature.hpp
    util/steam.cpp util/steam.hpp
    util/sysfont.cpp util/sysfont.hpp
    util/string.hpp util/vector.hpp

    # proxy
    proxy/winhttp.c proxy/winhttp.h proxy/winhttp.def

    LANGUAGES C CXX
    FOLDER app
    OUTPUT_NAME EROverlay
)

if(NOT MSVC)
    find_package(DirectXMath REQUIRED)
endif()
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC _CRT_SECURE_NO_WARNINGS PRIVATE EROVERLAY_EXPORTS)
endif()
if(NOT MSVC)
    target_compile_options(${PROJECT_NAME} PUBLIC -mavx2 -mlzcnt -mbmi -mbmi2)
endif()
target_include_directories(${PROJECT_NAME} PUBLIC .)
target_link_libraries(${PROJECT_NAME}
    # External 3rd party libraries
    PUBLIC
        imgui fmt::fmt nlohmann_json::nlohmann_json
    PRIVATE
        Pattern16 inih steam_api64 imgui_dx12_backend minhook
    # Windows libraries
        dwmapi shlwapi version)
if(NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE Microsoft::DirectXMath)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")

add_subdirectory(boss)
add_subdirectory(achievements)
add_subdirectory(minimap)
