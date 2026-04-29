string(REGEX REPLACE "^.*/" "" last "${BUILD_DIR}")

set(environment "${last}")

if(environment STREQUAL "master")
    set(src_root "master")
elseif(environment STREQUAL "media")
    set(src_root "media")
elseif(environment STREQUAL "gpu0" OR environment STREQUAL "gpu1")
    set(src_root "gpu")
elseif(environment STREQUAL "io")
    set(src_root "io")
else()
    message(FATAL_ERROR "Unknown build environment: '${BUILD_DIR}'")
endif()
message(STATUS "src_root is set to '${src_root}'")

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/${src_root}/*.*)

idf_component_register(
    SRCS ${app_sources}
)
