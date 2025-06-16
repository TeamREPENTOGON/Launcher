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

add_subdirectory(deps/bzip2)
add_subdirectory(deps/bsdiff)

# Generate the resource file
set (RESOURCE_PATH "${CMAKE_SOURCE_DIR}/self_updater_rsrc")
configure_file (${CMAKE_SOURCE_DIR}/self_updater/self_updater.rc.in ${CMAKE_SOURCE_DIR}/self_updater/self_updater.rc)

file (GLOB_RECURSE SHARED_FILES shared/*.cpp "include/shared/*.h")
add_library (shared STATIC ${SHARED_FILES})
target_include_directories (shared PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/deps/curl/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include")
target_compile_options (shared PUBLIC "/MD" "/w44061")
target_link_libraries (shared libcurl zip)

target_link_libraries (bspatch bzip2)
target_include_directories (bspatch PRIVATE 
    "${CMAKE_SOURCE_DIR}/deps/bzip2")

file (GLOB_RECURSE LAUNCHER_FILES src/*.cpp "include/launcher/*.h" self_updater/self_updater.rc)
add_executable (REPENTOGONLauncher WIN32 ${LAUNCHER_FILES})
target_compile_options (REPENTOGONLauncher PUBLIC "/MD" "/w44061")
target_include_directories (REPENTOGONLauncher PRIVATE 
    "${CMAKE_SOURCE_DIR}/deps/curl/include"
    "${CMAKE_SOURCE_DIR}/deps/wxWidgets/include"
    "${CMAKE_SOURCE_DIR}/deps"
    "${CMAKE_SOURCE_DIR}/deps/zlib"
    "${CMAKE_SOURCE_DIR}/deps/bzip2"
    "${CMAKE_SOURCE_DIR}/deps/bsdiff"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include"
    "${CMAKE_SOURCE_DIR}/deps/chained_futures/include"
)
target_compile_definitions (REPENTOGONLauncher PRIVATE UNICODE INI_SHARED_LIB "CMAKE_LAUNCHER_VERSION=\"${CMAKE_LAUNCHER_VERSION}\"")
target_link_libraries (REPENTOGONLauncher shared wxbase wxcore wxrichtext bcrypt inih libcurl zip Imagehlp bzip2 bspatch)
add_custom_command(
    TARGET REPENTOGONLauncher POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:REPENTOGONLauncher> $<TARGET_FILE_DIR:REPENTOGONLauncher> COMMAND_EXPAND_LISTS
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/patch"
        "$<TARGET_FILE_DIR:REPENTOGONLauncher>/patch"
)


file (GLOB_RECURSE UPDATER_FILES "include/self_updater/*.h" self_updater/*.cpp "include/launcher/version.h" src/version.cpp ${SHARED_FILES})
add_executable (REPENTOGONLauncherUpdater WIN32 ${UPDATER_FILES})
target_link_libraries (REPENTOGONLauncherUpdater libcurl_static zip zlibstatic userenv ktmw32 comctl32)
target_include_directories (REPENTOGONLauncherUpdater PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/deps/libzip/lib"
    "${CMAKE_SOURCE_DIR}/deps/curl/include"
    "${CMAKE_SOURCE_DIR}/deps/rapidjson/include")
target_compile_definitions (REPENTOGONLauncherUpdater PRIVATE "CMAKE_LAUNCHER_VERSION=\"${CMAKE_LAUNCHER_VERSION}\"")
target_compile_options (REPENTOGONLauncherUpdater PUBLIC "/MD" "/w44061")

add_custom_command (TARGET REPENTOGONLauncherUpdater POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:REPENTOGONLauncherUpdater> "${CMAKE_SOURCE_DIR}/self_updater_rsrc")

if (LAUNCHER_UNSTABLE)
    # add_subdirectory (testing)
    target_compile_definitions (REPENTOGONLauncher PRIVATE LAUNCHER_UNSTABLE)
endif()
