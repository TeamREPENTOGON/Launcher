set (INIH_DIR "${CMAKE_SOURCE_DIR}/deps/inih")
add_library (inih SHARED "${INIH_DIR}/ini.c" "${INIH_DIR}/cpp/INIReader.cpp" "${INIH_DIR}/ini.h" "${INIH_DIR}/cpp/INIReader.h")

target_compile_definitions (inih PUBLIC INI_SHARED_LIB)
target_compile_definitions (inih PRIVATE INI_SHARED_LIB_BUILDING)
target_compile_options (inih PUBLIC "/MD")