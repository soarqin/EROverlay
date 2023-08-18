project(minhook64 C)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message( FATAL_ERROR "This subset of minhook only supports x64 targets")
endif()

set(SOURCES_MINHOOK 
  "buffer.c"
  "hook.c"
  "trampoline.c"
)
set(SOURCES_HDE "hde64.c")

add_library(minhook STATIC ${SOURCES_MINHOOK} ${SOURCES_HDE})

target_include_directories(minhook PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
