cmake_minimum_required (VERSION 2.6)
project (Transport)
set (TRANSPORT_VERSION_MAJOR 0)
set (TRANSPORT_VERSION_MINOR 1)
configure_file (
	"${PROJECT_SOURCE_DIR}/conf.h.in"
	"${PROJECT_BINARY_DIR}/conf.h"
	)
include_directories("${PROJECT_BINARY_DIR}")
add_executable(Transport transport.c)

find_package (curl)
if (CURL_FOUND)
	include_directories(${CURL_INCLUDE_DIRS})
	target_link_libraries (Transport ${CURL_LIBRARIES})
endif (CURL_FOUND)

find_package (libconfig)
if (CONFIG_FOUND)
	include_directories(${CONFIG_INCLUDE_DIR})
	target_link_libraries (Transport ${CONFIG_LIBRARY})
endif (CONFIG_FOUND)

install (TARGETS Transport DESTINATION bin)
install (FILES transport.h DESTINATION include)
install (FILES "${PROJECT_BINARY_DIR}/config.h" DESTINATION include)