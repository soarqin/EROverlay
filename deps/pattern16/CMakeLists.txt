project(Pattern16 VERSION 3.11.2 LANGUAGES CXX)

add_library(Pattern16 INTERFACE)
target_compile_features(Pattern16 INTERFACE cxx_std_17)
target_include_directories(Pattern16 INTERFACE .)
