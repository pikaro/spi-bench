string(REGEX REPLACE "^.*/" "" last "${BUILD_DIR}")
set(environment "${last}")
if(environment STREQUAL "master")
    set(src_root "master")
elseif(environment STREQUAL "slave")
    set(src_root "slave")
else()
    message(FATAL_ERROR "Unknown build environment: '${environment}'")
endif()
message(STATUS "src_root is set to '${src_root}'")

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/${src_root}/*.*)

idf_component_register(
    SRCS ${app_sources}
)
