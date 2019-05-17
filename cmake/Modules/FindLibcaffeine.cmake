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
set(LIBCAFFEINE_FOUND_CPACK FALSE)
set(LIBCAFFEINE_FOUND_PROJECT FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(LIBCAFFEINE_REQUIRES_LIBRARY FALSE)
else()
    set(LIBCAFFEINE_REQUIRES_LIBRARY TRUE)
endif()

function(find_libcaffeine_cpack)
    math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")

    find_file(LIBCAFFEINE_CMAKE_FILE
        NAMES
            libcaffeineConfig.cmake
        HINTS
            ${LIBCAFFEINE_DIR${BITS}}
            ${LIBCAFFEINE_DIR}
            ${DepsPath${BITS}}
            ${DepsPath}
            ENV LIBCAFFEINE_DIR${BITS}
            ENV LIBCAFFEINE_DIR
            ENV DepsPath${BITS}
            ENV DepsPath
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

    if(EXISTS "${LIBCAFFEINE_CMAKE_FILE}")
        set(LIBCAFFEINE_FOUND TRUE PARENT_SCOPE)
        set(LIBCAFFEINE_FOUND_CPACK TRUE PARENT_SCOPE)
        unset(LIBCAFFEINE_INCLUDE_DIR CACHE)
        unset(LIBCAFFEINE_PATH_RELEASE CACHE)
        unset(LIBCAFFEINE_PATH_DEBUG CACHE)
        unset(LIBCAFFEINE_LIBRARY_RELWITHDEBINFO CACHE)
        unset(LIBCAFFEINE_BINARY_RELWITHDEBINFO CACHE)
        unset(LIBCAFFEINE_BINARY_PDB_RELWITHDEBINFO CACHE)
        unset(LIBCAFFEINE_LIBRARY_DEBUG CACHE)
        unset(LIBCAFFEINE_BINARY_DEBUG CACHE)
        unset(LIBCAFFEINE_BINARY_PDB_DEBUG CACHE)
        unset(LIBCAFFEINE_LIBRARY_RELEASE CACHE)
        unset(LIBCAFFEINE_BINARY_RELEASE CACHE)
        unset(LIBCAFFEINE_LIBRARY_MINSIZEREL CACHE)
        unset(LIBCAFFEINE_BINARY_MINSIZEREL CACHE)
        return()
    endif()
endfunction()

function(find_libcaffeine_project)
    if(LIBCAFFEINE_FOUND)
        return()
    endif()

    # This is sort of a hack to define the same variables as the CPack config does.
    # If possible, prefer the CPack version instead.

    math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(BIN_SUFFIX "dylib")
        set(LIB_SUFFIX "a")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(BIN_SUFFIX "so")
        set(LIB_SUFFIX "a")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(BIN_SUFFIX "dll")
        if(MSVC)
            set(LIB_SUFFIX "lib")
        else()
            set(LIB_SUFFIX "a")
        endif()
    endif()

    # Search for include/caffeine.h
    find_path(LIBCAFFEINE_INCLUDE_DIR
        NAMES
            caffeine.h
        HINTS
            ${LIBCAFFEINE_DIR${BITS}}
            ${LIBCAFFEINE_DIR}
            ENV LIBCAFFEINE_DIR${BITS}
            ENV LIBCAFFEINE_DIR
        PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            /opt/local
            /sw/include
            ~/Library/Frameworks
            /Library/Frameworks
        PATH_SUFFIXES
            include
            inc
    )
    if(NOT EXISTS "${LIBCAFFEINE_INCLUDE_DIR}")
        return()
    endif()

    # Search for libcaffeine, libcaffeiner or libcaffeines.
    find_path(LIBCAFFEINE_PATH_RELEASE
        NAMES
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
        HINTS
            ${LIBCAFFEINE_DIR${BITS}}
            ${LIBCAFFEINE_DIR}
            ENV LIBCAFFEINE_DIR${BITS}
            ENV LIBCAFFEINE_DIR
        PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            /opt/local
            /sw/include
            ~/Library/Frameworks
            /Library/Frameworks
        PATH_SUFFIXES
            build/${BITS}/RelWithDebInfo
            build${BITS}/RelWithDebInfo
            build/RelWithDebInfo
            build/${BITS}/Release
            build${BITS}/Release
            build/Release
            build/${BITS}/MinSizeRel
            build${BITS}/MinSizeRel
            build/MinSizeRel
    )
    if(NOT EXISTS "${LIBCAFFEINE_PATH_RELEASE}" AND LIBCAFFEINE_REQUIRES_LIBRARY)
        return()
    endif()

    # Search for libcaffeined, libcaffeine, libcaffeiner, or libcaffeines.
    find_path(LIBCAFFEINE_PATH_DEBUG
        NAMES
            libcaffeined.${BIN_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
        HINTS
            ${LIBCAFFEINE_DIR${BITS}}
            ${LIBCAFFEINE_DIR}
            ENV LIBCAFFEINE_DIR${BITS}
            ENV LIBCAFFEINE_DIR
        PATHS
            /usr/include
            /usr/local/include
            /opt/local/include
            /opt/local
            /sw/include
            ~/Library/Frameworks
            /Library/Frameworks
        PATH_SUFFIXES
            build/${BITS}/Debug
            build${BITS}/Debug
            build/Debug
            build/${BITS}/RelWithDebInfo
            build${BITS}/RelWithDebInfo
            build/RelWithDebInfo
            build/${BITS}/Release
            build${BITS}/Release
            build/Release
            build/${BITS}/MinSizeRel
            build${BITS}/MinSizeRel
            build/MinSizeRel
    )
    if(NOT EXISTS "${LIBCAFFEINE_PATH_DEBUG}")
        return()
    endif()

    # Find Library and Binary files
    find_file(LIBCAFFEINE_LIBRARY_RELEASE
        NAMES
            libcaffeine.${LIB_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    find_file(LIBCAFFEINE_BINARY_RELEASE
        NAMES
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    find_file(LIBCAFFEINE_LIBRARY_DEBUG
        NAMES
            libcaffeined.${LIB_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_DEBUG}
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    find_file(LIBCAFFEINE_BINARY_DEBUG
        NAMES
            libcaffeined.${BIN_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_DEBUG}
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_RELEASE}" AND LIBCAFFEINE_REQUIRES_LIBRARY)
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_RELEASE}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_DEBUG}" AND LIBCAFFEINE_REQUIRES_LIBRARY)
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_DEBUG}")
        return()
    endif()

    # Define additional libraries (share the same binary as Release).
    set(LIBCAFFEINE_BINARY_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_BINARY_MINSIZEREL "${LIBCAFFEINE_BINARY_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY_MINSIZEREL "${LIBCAFFEINE_LIBRARY_RELEASE}" PARENT_SCOPE)

    # Define remaining variables.
    set(LIBCAFFEINE_BINARY "${LIBCAFFEINE_BINARY_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY "${LIBCAFFEINE_LIBRARY_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_BINARY_DIR "${LIBCAFFEINE_PATH_RELEASE}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY_DIR "${LIBCAFFEINE_PATH_RELEASE}" PARENT_SCOPE)

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(MSVC)
            # Find PDB files if Windows and MSVC.
            find_file(LIBCAFFEINE_BINARY_PDB_RELWITHDEBINFO
                NAMES
                    libcaffeine.pdb
                HINTS
                    ${LIBCAFFEINE_PATH_RELEASE}
            )
            find_file(LIBCAFFEINE_BINARY_PDB_DEBUG
                NAMES
                    libcaffeined.pdb
                    libcaffeine.pdb
                HINTS
                    ${LIBCAFFEINE_PATH_DEBUG}
                    ${LIBCAFFEINE_PATH_RELEASE}
            )
            set(LIBCAFFEINE_BINARY_PDB "${LIBCAFFEINE_BINARY_PDB_RELWITHDEBINFO}" PARENT_SCOPE)
        endif()
    endif()

    # Define target for linking.
    if(NOT TARGET libcaffeine)
        add_library(libcaffeine SHARED IMPORTED)
        set_target_properties(libcaffeine PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBCAFFEINE_INCLUDE_DIR}"
            INTERFACE_SOURCES "${LIBCAFFEINE_INCLUDE_DIR}/caffeine.h"
        )
        set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
        set_target_properties(libcaffeine PROPERTIES
            IMPORTED_IMPLIB_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_RELEASE}"
        )
        set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
        set_target_properties(libcaffeine PROPERTIES
            IMPORTED_IMPLIB_RELEASE "${LIBCAFFEINE_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_RELEASE "${LIBCAFFEINE_BINARY_RELEASE}"
        )
        set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
        set_target_properties(libcaffeine PROPERTIES
            IMPORTED_IMPLIB_MINSIZEREL "${LIBCAFFEINE_LIBRARY_RELEASE}"
            IMPORTED_LOCATION_MINSIZEREL "${LIBCAFFEINE_BINARY_RELEASE}"
        )
        set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
        set_target_properties(libcaffeine PROPERTIES
            IMPORTED_IMPLIB_DEBUG "${LIBCAFFEINE_LIBRARY_DEBUG}"
            IMPORTED_LOCATION_DEBUG "${LIBCAFFEINE_BINARY_DEBUG}"
        )
    else()
        message("Target libcaffeine already defined, skipping.")
    endif()

    unset(LIBCAFFEINE_PATH_DEBUG CACHE)
    unset(LIBCAFFEINE_PATH_RELEASE CACHE)
    set(LIBCAFFEINE_FOUND TRUE PARENT_SCOPE)
    set(LIBCAFFEINE_FOUND_PROJECT TRUE PARENT_SCOPE)
endfunction()

include(FindPackageHandleStandardArgs)

# Find by CPack
find_libcaffeine_cpack()
if(LIBCAFFEINE_FOUND_CPACK)
    find_package_handle_standard_args(
        LIBCAFFEINE
        FOUND_VAR LIBCAFFEINE_FOUND
        REQUIRED_VARS
            LIBCAFFEINE_CMAKE_FILE
    )
    include("${LIBCAFFEINE_CMAKE_FILE}")
else()
    # Find by Project
    find_libcaffeine_project()

    if(LIBCAFFEINE_REQUIRES_LIBRARY)
        set(LIBCAFFEINE_LIBRARIES LIBCAFFEINE_LIBRARY_RELWITHDEBINFO LIBCAFFEINE_LIBRARY_DEBUG)
    endif()

    find_package_handle_standard_args(
        LIBCAFFEINE
        FOUND_VAR LIBCAFFEINE_FOUND
        REQUIRED_VARS
            LIBCAFFEINE_INCLUDE_DIR
            LIBCAFFEINE_BINARY_RELWITHDEBINFO
            LIBCAFFEINE_BINARY_DEBUG
            ${LIBCAFFEINE_LIBRARIES}
    )
endif()
