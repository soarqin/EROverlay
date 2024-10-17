project(dlfcn-win32 C)

add_library(dl STATIC dlfcn.c dlfcn.h)
target_include_directories(dl PUBLIC ${PROJECT_SOURCE_DIR})
