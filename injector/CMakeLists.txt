add_project(injector WIN32
    main.c
    LANGUAGES C
    FOLDER app
    )
target_compile_definitions(${PROJECT_NAME} PRIVATE _UNICODE UNICODE _CRT_SECURE_NO_WARNINGS)

if(NOT MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE -municode)
endif()
