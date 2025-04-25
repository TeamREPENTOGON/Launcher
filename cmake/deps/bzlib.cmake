cmake_minimum_required(VERSION 3.0)
project(bzip2 C)

set(BZIP2_SOURCES
    blocksort.c
    bzlib.c
    compress.c
    crctable.c
    decompress.c
    huffman.c
    randtable.c
)

add_library(bzip2 STATIC ${BZIP2_SOURCES})
target_include_directories(bzip2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bzip2 PRIVATE BZ_NO_STDIO)