project(steam_api64 C)

add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE include)
target_link_directories(${PROJECT_NAME} INTERFACE lib)
target_link_libraries(${PROJECT_NAME} INTERFACE steam_api64.lib)
