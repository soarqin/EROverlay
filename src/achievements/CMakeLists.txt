project(achievements CXX)

add_library(${PROJECT_NAME} SHARED
    dllmain.cpp
    data.cpp data.hpp
    render.cpp render.hpp
)

target_link_libraries(${PROJECT_NAME} EROverlayDLL minhook steam_api64)

set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<0:>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/overlays/$<0:>"
    ARCHIVE_OUTPUT_NAME "Achievements"
    RUNTIME_OUTPUT_NAME "Achievements")
