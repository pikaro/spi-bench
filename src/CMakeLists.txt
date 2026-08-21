string(REGEX REPLACE "^.*/" "" last "${BUILD_DIR}")

set(environment "${last}")
string(REGEX REPLACE "-.*$" "" environment_base "${environment}")

if(DEFINED SRC_ROOT)
    set(src_root "${SRC_ROOT}")
elseif(environment_base STREQUAL "master")
    set(src_root "master")
elseif(environment_base STREQUAL "media")
    set(src_root "media")
elseif(environment_base STREQUAL "gpu0" OR environment_base STREQUAL "gpu1")
    set(src_root "gpu")
elseif(environment_base STREQUAL "io")
    set(src_root "io")
elseif(environment_base STREQUAL "ai")
    set(src_root "ai")
elseif(environment_base STREQUAL "scratch")
    set(src_root "scratch")
else()
    message(FATAL_ERROR "Unknown build environment: '${BUILD_DIR}'")
endif()
message(STATUS "src_root is set to '${src_root}'")

file(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/${src_root}/*.*)

if(src_root STREQUAL "media" AND ENABLE_BLUETOOTH AND NOT ENABLE_BTSTACK)
    set(a2dp_src_root "${CMAKE_SOURCE_DIR}/.pio/libdeps/${environment_base}/ESP32-A2DP/src")
    if(IS_DIRECTORY "${a2dp_src_root}")
        list(APPEND app_sources
            "${a2dp_src_root}/BluetoothA2DPCommon.cpp"
            "${a2dp_src_root}/BluetoothA2DPOutput.cpp"
            "${a2dp_src_root}/BluetoothA2DPSink.cpp"
        )
    endif()
endif()

set(app_requires)
if(ENABLE_BTSTACK OR environment STREQUAL "media-btstack")
    list(APPEND app_requires btstack)
endif()
if(src_root STREQUAL "ai")
    list(APPEND app_requires
        audio_afe_esp_sr
        espressif__esp-dsp
        esp_netif
        esp_ringbuf
        json
        mbedtls
        tcp_transport
    )
endif()

idf_component_register(
    SRCS ${app_sources}
    REQUIRES ${app_requires}
)
