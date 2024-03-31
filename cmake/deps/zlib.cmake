target_compile_definitions (zlib PRIVATE ZLIB_DLL ZLIB_INTERNAL)
add_library (ZLIB::ZLIB ALIAS zlib)
