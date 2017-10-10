########################################################################
# Project setup
########################################################################
cmake_minimum_required(VERSION 2.8.9)
project(DSCope CXX)

include(FindPkgConfig)

if(${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})
    find_package(Pothos "0.5.1" CONFIG REQUIRED)
else()
    find_package(Pothos CONFIG REQUIRED) #in-tree build
endif()


#===============================================================================
#= User Options
#-------------------------------------------------------------------------------

option(DISABLE_WERROR "Build without -Werror" TRUE)
option(ENABLE_SIGNALS "Build with UNIX signals" TRUE)
option(ENABLE_DECODE "Build with libsigrokdecode4DSL" FALSE)
option(STATIC_PKGDEPS_LIBS "Statically link to (pkg-config) libraries" FALSE)


#===============================================================================
#= Dependencies
#-------------------------------------------------------------------------------

list(APPEND PKGDEPS
	"libsigrok4DSL >= 0.2.0"
	"libusb-1.0 >= 1.0.16"
	"libzip >= 0.10"
)

find_package(PkgConfig)
pkg_check_modules(PKGDEPS REQUIRED ${PKGDEPS})

find_package(Threads)

find_package(Boost 1.42 COMPONENTS filesystem system thread REQUIRED)

########################################################################
# json.hpp header
########################################################################
find_path(JSON_HPP_INCLUDE_DIR NAMES json.hpp PATH_SUFFIXES nlohmann)

if (NOT JSON_HPP_INCLUDE_DIR)
    message(WARNING "Pothos Dscope blocks require json.hpp, skipping...")
endif (NOT JSON_HPP_INCLUDE_DIR)

########################################################################
## Feature registration
########################################################################
include(FeatureSummary)
include(CMakeDependentOption)
cmake_dependent_option(ENABLE_DSCOPE "Enable Pothos DSCope component" ON "Pothos_FOUND;JSON_HPP_INCLUDE_DIR" OFF)
add_feature_info(Audio ENABLE_DSCOPE "Streaming audio source and sink blocks")
#if (NOT ENABLE_DSCOPE)
#    message(ERROR "what the fxxk!")
#    return()
#endif()

########################################################################
## Build and install module
########################################################################
include_directories(${PKGDEPS_INCLUDE_DIRS})
add_definitions(${PKGDEPS_DEFINITIONS})
#include_directories(${JSON_HPP_INCLUDE_DIR})

#the number of frames to block on an IO call
#when no non-blocking frames are available.
add_definitions(-DMIN_FRAMES_BLOCKING=1024)

POTHOS_MODULE_UTIL(
    TARGET DscopeSupport
    SOURCES
        DscopeSource.cpp
        devicemanager.cpp
        sigsession.cpp
        device.cpp
        devinst.cpp
    LIBRARIES ${PKGDEPS_LIBRARIES} ${Boost_LIBRARIES}
    DESTINATION dscope
    ENABLE_DOCS
)
