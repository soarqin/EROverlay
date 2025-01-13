project(steam_api64 C)

file(GLOB STEAMWORKS_HEADERS include/steam/*.h)
add_library(${PROJECT_NAME} INTERFACE ${STEAMWORKS_HEADERS})
target_include_directories(${PROJECT_NAME} INTERFACE include)
target_link_directories(${PROJECT_NAME} INTERFACE lib)
target_link_libraries(${PROJECT_NAME} INTERFACE steam_api64.lib)
