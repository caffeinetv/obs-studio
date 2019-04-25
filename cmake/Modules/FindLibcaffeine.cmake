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

    find_path(LIBCAFFEINE_PATH_RELWITHDEBINFO
        NAMES
            libcaffeine.${LIB_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
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
    find_path(LIBCAFFEINE_PATH_RELEASE
        NAMES
            libcaffeiner.${LIB_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
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
            build/${BITS}/Release
            build${BITS}/Release
            build/Release
            build/${BITS}/RelWithDebInfo
            build${BITS}/RelWithDebInfo
            build/RelWithDebInfo
            build/${BITS}/MinSizeRel
            build${BITS}/MinSizeRel
            build/MinSizeRel
    )
    find_path(LIBCAFFEINE_PATH_MINSIZEREL
        NAMES
            libcaffeines.${LIB_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
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
            build/${BITS}/MinSizeRel
            build${BITS}/MinSizeRel
            build/MinSizeRel
            build/${BITS}/RelWithDebInfo
            build${BITS}/RelWithDebInfo
            build/RelWithDebInfo
            build/${BITS}/Release
            build${BITS}/Release
            build/Release
    )
    find_path(LIBCAFFEINE_PATH_DEBUG
        NAMES
            libcaffeined.${LIB_SUFFIX}
            libcaffeined.${BIN_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeiner.${BIN_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
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

    if(NOT EXISTS "${LIBCAFFEINE_PATH_RELWITHDEBINFO}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_PATH_MINSIZEREL}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_PATH_RELEASE}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_PATH_DEBUG}")
        return()
    endif()
    
    find_file(LIBCAFFEINE_LIBRARY_RELWITHDEBINFO
        NAMES
            libcaffeine.${LIB_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_RELEASE}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
    )
    find_file(LIBCAFFEINE_BINARY_RELWITHDEBINFO
        NAMES
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_RELEASE}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
    )
    find_file(LIBCAFFEINE_LIBRARY_MINSIZEREL
        NAMES 
            libcaffeines.${LIB_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_MINSIZEREL}
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    find_file(LIBCAFFEINE_BINARY_MINSIZEREL
        NAMES 
            libcaffeines.${BIN_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeiner.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_MINSIZEREL}
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_RELEASE}
    )
    find_file(LIBCAFFEINE_LIBRARY_RELEASE
        NAMES 
            libcaffeiner.${LIB_SUFFIX}
            libcaffeine.${LIB_SUFFIX}
            libcaffeines.${LIB_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELEASE}
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
    )
    find_file(LIBCAFFEINE_BINARY_RELEASE
        NAMES 
            libcaffeiner.${LIB_SUFFIX}
            libcaffeine.${BIN_SUFFIX}
            libcaffeines.${BIN_SUFFIX}
        PATHS
            ${LIBCAFFEINE_PATH_RELEASE}
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
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
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
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
            ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            ${LIBCAFFEINE_PATH_MINSIZEREL}
    )

    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_RELWITHDEBINFO}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_RELWITHDEBINFO}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_MINSIZEREL}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_MINSIZEREL}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_RELEASE}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_RELEASE}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_LIBRARY_DEBUG}")
        return()
    endif()
    if(NOT EXISTS "${LIBCAFFEINE_BINARY_DEBUG}")
        return()
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        if(MSVC)
            find_file(LIBCAFFEINE_BINARY_PDB_RELWITHDEBINFO
                NAMES
                    libcaffeine.pdb
                    libcaffeined.pdb
                HINTS
                    ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            )
            find_file(LIBCAFFEINE_BINARY_PDB_DEBUG
                NAMES
                    libcaffeine.pdb
                    libcaffeined.pdb
                HINTS
                    ${LIBCAFFEINE_PATH_DEBUG}
                    ${LIBCAFFEINE_PATH_RELWITHDEBINFO}
            )
            set(LIBCAFFEINE_BINARY_PDB "${LIBCAFFEINE_BINARY_PDB_RELWITHDEBINFO}" PARENT_SCOPE)
        endif()
    endif()

    set(LIBCAFFEINE_BINARY "${LIBCAFFEINE_BINARY_RELWITHDEBINFO}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY "${LIBCAFFEINE_LIBRARY_RELWITHDEBINFO}" PARENT_SCOPE)
    set(LIBCAFFEINE_BINARY_DIR "${LIBCAFFEINE_PATH_RELWITHDEBINFO}" PARENT_SCOPE)
    set(LIBCAFFEINE_LIBRARY_DIR "${LIBCAFFEINE_PATH_RELWITHDEBINFO}" PARENT_SCOPE)
    
    if(NOT TARGET libcaffeine)
        add_library(libcaffeine SHARED IMPORTED)
        set_target_properties(libcaffeine PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${LIBCAFFEINE_INCLUDE_DIR}"
            INTERFACE_SOURCES "${LIBCAFFEINE_INCLUDE_DIR}/caffeine.h"
        )
        if(EXISTS "${LIBCAFFEINE_BINARY_RELWITHDEBINFO}")
            set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
            set_target_properties(libcaffeine PROPERTIES
                IMPORTED_IMPLIB_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_RELWITHDEBINFO}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_RELWITHDEBINFO}"
            )
        endif()
        if(EXISTS "${LIBCAFFEINE_BINARY_RELEASE}")
            set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
            set_target_properties(libcaffeine PROPERTIES
                IMPORTED_IMPLIB_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_RELEASE}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_RELEASE}"
            )
        endif()
        if(EXISTS "${LIBCAFFEINE_BINARY_MINSIZEREL}")
            set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
            set_target_properties(libcaffeine PROPERTIES
                IMPORTED_IMPLIB_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_MINSIZEREL}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_MINSIZEREL}"
            )
        endif()
        if(EXISTS "${LIBCAFFEINE_BINARY_DEBUG}")
            set_property(TARGET libcaffeine APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
            set_target_properties(libcaffeine PROPERTIES
                IMPORTED_IMPLIB_RELWITHDEBINFO "${LIBCAFFEINE_LIBRARY_DEBUG}"
                IMPORTED_LOCATION_RELWITHDEBINFO "${LIBCAFFEINE_BINARY_DEBUG}"
            )
        endif()
    else()
        message("Target libcaffeine already defined, skipping.")
    endif()
    
    set(LIBCAFFEINE_FOUND TRUE PARENT_SCOPE)
endfunction()

include(FindPackageHandleStandardArgs)

# CPack Archive
find_libcaffeine_cpack()
if(LIBCAFFEINE_FOUND_CPACK)
    find_package_handle_standard_args(
        LIBCAFFEINE
        FOUND_VAR LIBCAFFEINE_FOUND
        REQUIRED_VARS
            LIBCAFFEINE_CMAKE_FILE
    )
    include("${LIBCAFFEINE_CMAKE_FILE}")
endif()

# Project
if(LIBCAFFEINE_FOUND_CPACK)
    return()
endif()
find_libcaffeine_project()
find_package_handle_standard_args(
    LIBCAFFEINE
    FOUND_VAR LIBCAFFEINE_FOUND
    REQUIRED_VARS
        LIBCAFFEINE_INCLUDE_DIR
        LIBCAFFEINE_LIBRARY_RELWITHDEBINFO
        LIBCAFFEINE_BINARY_RELWITHDEBINFO
        LIBCAFFEINE_LIBRARY_DEBUG
        LIBCAFFEINE_BINARY_DEBUG
)
