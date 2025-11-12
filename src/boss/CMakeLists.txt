add_project(boss SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp

    LANGUAGES CXX
    NO_PREFIX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Boss
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL nlohmann_json::nlohmann_json inih)
