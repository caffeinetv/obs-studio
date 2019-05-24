project(caffeine)

find_package(libcaffeine REQUIRED)
find_package(Threads REQUIRED)

if(MSVC)
	set(caffeine_PLATFORM_DEPS
		shlwapi.lib
		w32-pthreads)
endif()

set(caffeine_HEADERS
	caffeine-foreground-process.h
	caffeine-settings.h)

set(caffeine_SOURCES
	caffeine-foreground-process.c
	caffeine-module.c
	caffeine-output.c)

add_library(caffeine MODULE
	${caffeine_HEADERS}
	${caffeine_SOURCES})

target_link_libraries(caffeine
	${caffeine_PLATFORM_DEPS}
	${THREADS_LIBRARIES}
	libobs
	libcaffeine)

install_obs_plugin_with_data(caffeine data)

# TODO: figure out how to get libcaffeine copied to the right place on Mac & Linux
# TODO: this sort of works on mac, but CI script fails. It doesn't do the right thing on linux
#install_obs_bin(caffeine "" ${CAFFEINE_SHARED})
