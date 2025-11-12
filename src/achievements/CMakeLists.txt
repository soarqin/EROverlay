add_project(achievements SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp

    LANGUAGES CXX
    NO_PREFIX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Achievements
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL minhook steam_api64)
