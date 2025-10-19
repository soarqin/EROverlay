add_project(minimap SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp
    defs/BonfireWarpParam.h
    defs/WorldMapLegacyConvParam.h

    LANGUAGES CXX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Minimap
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
