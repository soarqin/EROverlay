cmake_minimum_required(VERSION 3.13)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(GlobalOptions)
add_global_options(CXX_STANDARD 23)
include(CustomCompilerOptions)
fix_compile_flags()
fix_release_flags(STRIP_BINARY ON USE_LTO OFF USE_STATIC_CRT ON)
include(ProjectMacros)

project(EROverlay)

add_subdirectory(deps)
add_subdirectory(src)
add_subdirectory(injector)