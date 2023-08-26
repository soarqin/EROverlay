project(inih C)

add_library(${PROJECT_NAME} STATIC ini.c ini.h)
target_include_directories(${PROJECT_NAME} PUBLIC .)
