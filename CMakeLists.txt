cmake_minimum_required (VERSION 3.20)
project (Launcher CXX)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/;${CMAKE_MODULE_PATH}")

option (LAUNCHER_TESTING "Build testing version instead of release version" OFF)

# Stupid MSVC
add_compile_definitions (_CRT_SECURE_NO_WARNINGS)

include (cmake/deps/curl_pre.cmake)
add_subdirectory (deps/curl)
include (cmake/deps/curl.cmake)

include (cmake/deps/inih.cmake)

include (cmake/deps/wxWidgets_pre.cmake)
add_subdirectory (deps/wxWidgets)
include (cmake/deps/wxWidgets.cmake)

include (cmake/deps/zlib_pre.cmake)
add_subdirectory (deps/zlib)
include (cmake/deps/zlib.cmake)

include (cmake/deps/libzip_pre.cmake)
add_subdirectory (deps/libzip)
include (cmake/deps/libzip.cmake)

file (GLOB_RECURSE LAUNCHER_FILES src/*.cpp src/*.h)
add_executable (REPENTOGONLauncher WIN32 ${LAUNCHER_FILES})
target_compile_options (REPENTOGONLauncher PUBLIC "/MD")
target_include_directories (REPENTOGONLauncher PRIVATE 
    "${CMAKE_SOURCE_DIR}/deps/curl/include" 
    "${CMAKE_SOURCE_DIR}/deps/wxWidgets/include" 
    "${CMAKE_SOURCE_DIR}/deps"
    "${CMAKE_SOURCE_DIR}/deps/zlib"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include")
target_compile_definitions (REPENTOGONLauncher PRIVATE UNICODE INI_SHARED_LIB)
target_link_libraries (REPENTOGONLauncher wxbase wxcore bcrypt inih libcurl zip Imagehlp)
add_custom_command (TARGET REPENTOGONLauncher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:REPENTOGONLauncher> $<TARGET_FILE_DIR:REPENTOGONLauncher> COMMAND_EXPAND_LISTS)

if (LAUNCHER_TESTING)
    add_subdirectory (testing)
    target_compile_definitions (REPENTOGONLauncher PRIVATE LAUNCHER_TESTING)
endif()
