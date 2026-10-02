add_project(minimap SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp
    gfx.cpp gfx.hpp
    resources.cpp resources.hpp
    layout.hpp ../util/paramreader.hpp
    ../util/assets.cpp ../util/assets.hpp
    defs/BonfireWarpParam.h
    defs/WorldMapLegacyConvParam.h
    defs/WorldMapPointParam.h

    LANGUAGES CXX
    NO_PREFIX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Minimap
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL xmllite shlwapi)
