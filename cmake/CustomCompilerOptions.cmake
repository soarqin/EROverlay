macro(fix_compile_flags)
    add_compile_options($<$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>:/utf-8>)
endmacro()

include(CMakeParseArguments)

macro(fix_release_flags)
    cmake_parse_arguments(FLAGS "" "STRIP_BINARY;USE_LTO;USE_STATIC_CRT" "" ${ARGN})
    option(RELEASE_STRIP_BINARY "Strip binaries for release configs" ${FLAGS_STRIP_BINARY})
    option(RELEASE_USE_LTO "Use link-time optimization for release configs" ${FLAGS_USE_LTO})
    option(RELEASE_USE_STATIC_CRT "Use static C/C++ runtime for release configs" ${FLAGS_USE_STATIC_CRT})

    if (RELEASE_STRIP_BINARY)
        add_link_options(
            $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-s>
        )
    endif ()
    if (RELEASE_USE_LTO)
        add_compile_options(
            $<$<AND:$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:/GL>
            $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-flto>
        )
        add_link_options(
            $<$<AND:$<C_COMPILER_ID:MSVC>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:/LTCG>
            $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-flto>
        )
    endif ()
    if (RELEASE_USE_STATIC_CRT)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        add_link_options(
            $<$<AND:$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static>
            $<$<AND:$<C_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libgcc>
            $<$<AND:$<CXX_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libstdc++>
        )
    endif ()
endmacro()
