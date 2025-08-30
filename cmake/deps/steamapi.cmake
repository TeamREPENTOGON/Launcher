cmake_minimum_required(VERSION 3.0)

add_library(steamapi SHARED IMPORTED GLOBAL)


set_target_properties(steamapi PROPERTIES
	INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/deps/steamapi"
)

set_target_properties(steamapi PROPERTIES
	IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/deps/steamapi/lib/steam_api.dll"
	IMPORTED_IMPLIB "${CMAKE_SOURCE_DIR}/deps/steamapi/lib/steam_api.lib"
)
