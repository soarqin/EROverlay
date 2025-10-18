add_project(boss SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp

    LANGUAGES CXX
    FOLDER overlays
    OUTPUT_SUBDIR overlays
    OUTPUT_NAME Boss
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL nlohmann_json::nlohmann_json inih)

set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
