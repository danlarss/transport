cmake_minimum_required (VERSION 2.6)
project (Transport)
set (TRANSPORT_VERSION_MAJOR 0)
set (TRANSPORT_VERSION_MINOR 1)
configure_file (
	"${PROJECT_SOURCE_DIR}/transport.h.in"
	"${PROJECT_BINARY_DIR}/transport.h"
	)
include_directories("${PROJECT_BINARY_DIR}")
add_executable(transport transport.c)

find_package (curl)
if (CURL_FOUND)
	include_directories(${CURL_INCLUDE_DIRS})
	target_link_libraries (transport ${CURL_LIBRARIES})
endif (CURL_FOUND)

find_package (libconfig)
if (CONFIG_FOUND)
	include_directories(${CONFIG_INCLUDE_DIR})
	target_link_libraries (transport ${CONFIG_LIBRARY})
endif (CONFIG_FOUND)

install (TARGETS transport DESTINATION bin)
install (FILES "${PROJECT_BINARY_DIR}/transport.h" DESTINATION include)