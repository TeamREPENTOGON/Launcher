cmake_minimum_required(VERSION 3.5)
project(bsdiff C)


set(BSPATCH_SOURCES
    bsdiff.c
    bsdiff.h
    bsdifflib.c
    bsdifflib.h
    bspatch.c
    bspatch.h
    bspatchlib.c
    bspatchlib.h
)

# Add bspatch as a library target
add_library(bspatch STATIC ${BSPATCH_SOURCES})

# if it expects any special compile flags, you can add them here:
# target_compile_definitions(bspatch PRIVATE BSDIFF_MINSIZE=0)
