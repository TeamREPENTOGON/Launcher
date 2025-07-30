cmake_minimum_required(VERSION 3.5)
project(bzip2 C)

set(BZIP2_SOURCES
    blocksort.c
    bzlib.c
    compress.c
    crctable.c
    decompress.c
    huffman.c
    randtable.c
    bzlib_private.h
)

add_library(bzip2 STATIC ${BZIP2_SOURCES})
target_include_directories(bzip2 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bzip2 PRIVATE)