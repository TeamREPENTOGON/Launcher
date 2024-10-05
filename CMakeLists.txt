cmake_minimum_required (VERSION 3.20)
project (Launcher CXX)
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/;${CMAKE_MODULE_PATH}")

option (LAUNCHER_UNSTABLE "Build unstable version instead of release version" OFF)

if (NOT CMAKE_LAUNCHER_VERSION)
  set (CMAKE_LAUNCHER_VERSION "dev")
endif()

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

# Generate the resource file
set (RESOURCE_PATH "${CMAKE_SOURCE_DIR}/updater_rsrc")
configure_file (${CMAKE_SOURCE_DIR}/self_updater/updater.rc.in ${CMAKE_SOURCE_DIR}/self_updater/updater.rc)

file (GLOB_RECURSE SHARED_FILES shared/*.cpp "include/shared/*.h")
add_library (shared STATIC ${SHARED_FILES})
target_include_directories (shared PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/curl/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include")
target_compile_options (shared PUBLIC "/MD")
target_link_libraries (shared libcurl)

file (GLOB_RECURSE LAUNCHER_FILES src/*.cpp "include/launcher/*.h" self_updater/updater.rc)
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
target_link_libraries (REPENTOGONLauncher shared wxbase wxcore bcrypt inih libcurl zip Imagehlp)
add_custom_command (TARGET REPENTOGONLauncher POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:REPENTOGONLauncher> $<TARGET_FILE_DIR:REPENTOGONLauncher> COMMAND_EXPAND_LISTS)

file (GLOB_RECURSE UPDATER_FILES "include/self_updater/*.h" self_updater/*.cpp)
add_executable (updater WIN32 ${UPDATER_FILES})
target_include_directories (updater PRIVATE
    "${CMAKE_SOURCE_DIR}/deps/curl/include" 
    "${CMAKE_SOURCE_DIR}/deps/wxWidgets/include" 
    "${CMAKE_SOURCE_DIR}/deps"
    "${CMAKE_SOURCE_DIR}/deps/zlib"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include")
target_compile_options (updater PUBLIC "/MD")

target_link_libraries (updater shared wxbase wxcore bcrypt inih libcurl zip Imagehlp)
add_custom_command (TARGET updater POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:updater> "${CMAKE_SOURCE_DIR}/updater_rsrc")

add_dependencies (REPENTOGONLauncher updater)

if (LAUNCHER_UNSTABLE)
    # add_subdirectory (testing)
    target_compile_definitions (REPENTOGONLauncher PRIVATE LAUNCHER_UNSTABLE)
endif()
