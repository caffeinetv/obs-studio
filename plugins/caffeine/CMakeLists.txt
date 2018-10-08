project(caffeine)

find_package(Caffeine)

if(NOT CAFFEINE_FOUND)
	message(STATUS "caffeine plugin disabled")
	return()
endif()

include_directories(
	${CAFFEINE_INCLUDE_DIR})

set(caffeine_SOURCES
	caffeine-module.c
	caffeine-output.c)

add_library(caffeine MODULE
	${caffeine_SOURCES}
	${caffeine_HEADERS})

target_link_libraries(caffeine
	${CAFFEINE_LIBRARIES}
	libobs)

#install_obs_plugin_with_data(caffeine data)
install_obs_plugin(caffeine)
