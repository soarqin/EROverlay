project(injector C)

add_executable(${PROJECT_NAME} WIN32 main.c)
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS)

if(NOT MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE -municode)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
