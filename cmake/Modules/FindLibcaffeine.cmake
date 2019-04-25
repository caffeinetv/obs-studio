# find_package handler for libcaffeine
#
# Parameters
# - LIBCAFFEINE_DIR: Path to libcaffeine source or cpack package
#
# Variables:
# - See libcaffeineConfig.cmake
# 
# Defined Targets:
# - libcaffeine
#

set(LIBCAFFEINE_DIR "" CACHE PATH "Path to libcaffeine")
set(LIBCAFFEINE_FOUND FALSE)

function(find_libcaffeine_cpack search_dir)
    if(NOT EXISTS "${search_dir}")
        set(LIBCAFFEINE_FOUND FALSE)
        return()
    endif()

    math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")

    find_file(LIBCAFFEINE_CMAKE_FILE
        NAMES
            libcaffeineConfig.cmake
        HINTS
            ${search_dir}
        PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            /opt/local
            /sw/include
            ~/Library/Frameworks
            /Library/Frameworks
        PATH_SUFFIXES
            lib${BITS}/cmake
            lib/${BITS}/cmake
            lib/cmake${BITS}
            lib${BITS}
            lib/${BITS}
            cmake${BITS}
            lib/cmake
            lib
            cmake
    )

    if(LIBCAFFEINE_CMAKE_FILE)
        set(LIBCAFFEINE_FOUND TRUE)
        return()
    endif()
endfunction()

find_libcaffeine_cpack(${LIBCAFFEINE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    LIBCAFFEINE
    FOUND_VAR LIBCAFFEINE_FOUND
    REQUIRED_VARS
        LIBCAFFEINE_CMAKE_FILE
)

if(LIBCAFFEINE_FOUND)
    include("${LIBCAFFEINE_CMAKE_FILE}")
endif()
