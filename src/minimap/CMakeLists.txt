add_project(minimap SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp
    defs/BonfireWarpParam.h
    defs/WorldMapLegacyConvParam.h

    LANGUAGES CXX
    NO_PREFIX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Minimap
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL)
