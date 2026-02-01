cmake_minimum_required(VERSION 3.16.0)

if(DEFINED SRC_ROOT AND NOT SRC_ROOT STREQUAL "")
  # ok
elseif(DEFINED CACHE{SRC_ROOT} AND NOT "$CACHE{SRC_ROOT}" STREQUAL "")
  set(SRC_ROOT "$CACHE{SRC_ROOT}")
else()
  message(FATAL_ERROR "SRC_ROOT is not set. Pass -DSRC_ROOT=<name> (e.g. master).")
endif()

if(NOT IS_DIRECTORY "${CMAKE_SOURCE_DIR}/src/${SRC_ROOT}")
  message(FATAL_ERROR "SRC_ROOT '${SRC_ROOT}' is not a valid directory.")
endif()

message(STATUS "Using defined SRC_ROOT='${SRC_ROOT}'")
set(SRC_ROOT "${SRC_ROOT}" CACHE STRING "Which src/<name> subtree to build" FORCE)

if(EXISTS "sdkconfig.${SRC_ROOT}.defaults")
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.${SRC_ROOT}.defaults")
    message(STATUS "-- Using per-env sdkconfig defaults: ${SDKCONFIG_DEFAULTS}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(spi)
