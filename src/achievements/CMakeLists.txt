add_project(achievements SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp

    LANGUAGES CXX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Achievements
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL minhook steam_api64)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
