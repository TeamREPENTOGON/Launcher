add_library (libzhl SHARED libzhl.cpp)
add_library (zhlRepentogon SHARED repentogon.cpp)
add_library (zhlDummy SHARED dummy.cpp)
add_executable (fakeIsaac fake_isaac.cpp)

set (LIBS_VERSION 1.0.6)

if (NOT ZHL_VERSION)
    set (ZHL_VERSION "${LIBS_VERSION}") 
endif()

if (NOT REPENTOGON_VERSION)
    set (REPENTOGON_VERSION "${LIBS_VERSION}")
endif()

if (NOT DUMMY_VERSION)
    set (DUMMY_VERSION "${LIBS_VERSION}")
endif ()

target_compile_definitions (libzhl PRIVATE ZHL_VERSION="${ZHL_VERSION}")
target_compile_definitions (zhlRepentogon PRIVATE REPENTOGON_VERSION="${REPENTOGON_VERSION}")
target_compile_definitions (zhlDummy PRIVATE DUMMY_VERSION="${DUMMY_VERSION}")