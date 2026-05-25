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

if(DEFINED SDKCONFIG_NAME AND NOT SDKCONFIG_NAME STREQUAL "")
  set(SDKCONFIG_DEFAULTS "sdkconfig.${SDKCONFIG_NAME}.defaults")
else()
  set(SDKCONFIG_DEFAULTS "sdkconfig.${SRC_ROOT}.defaults")
endif()

if(ENABLE_BTSTACK)
  if(DEFINED BTSTACK_ROOT AND IS_DIRECTORY "${BTSTACK_ROOT}")
    # explicit path supplied by the build environment
  elseif(DEFINED ENV{BTSTACK_ROOT} AND IS_DIRECTORY "$ENV{BTSTACK_ROOT}")
    set(BTSTACK_ROOT "$ENV{BTSTACK_ROOT}")
  elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack")
    set(BTSTACK_ROOT "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack")
  elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/BTstack")
    set(BTSTACK_ROOT "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/BTstack")
  elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack-eval")
    set(BTSTACK_ROOT "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack-eval")
  else()
    message(FATAL_ERROR "ENABLE_BTSTACK is ON but BTSTACK_ROOT was not found")
  endif()

  set(BTSTACK_ROOT "${BTSTACK_ROOT}" CACHE PATH "BTstack source root" FORCE)
  list(APPEND CMAKE_MODULE_PATH "${BTSTACK_ROOT}/tool/cmake")
endif()

# Not covered:

# app_trace
# app_update
# cmock
# esp_coex
# espcoredump
# esp_driver_ana_cmpr
# esp_driver_bitscrambler
# esp_driver_cam
# esp_driver_isp
# esp_driver_jpeg
# esp_driver_ledc
# esp_driver_mcpwm
# esp_driver_parlio
# esp_driver_pcnt
# esp_driver_ppa
# esp_driver_sdio
# esp_driver_sdm
# esp_driver_sdspi
# esp_driver_touch_sens
# esp_driver_tsens
# esp_eth
# esp_hid
# esp_http_client
# esp_https_ota
# esp_https_server
# esp_lcd
# esp_local_ctrl
# esp_netif
# esp_netif_stack
# esp_phy
# esp_pm
# esp_security
# esp-tls
# fatfs
# http_parser
# idf_test
# ieee802154
# json
# lwip
# mbedtls
# mqtt
# nvs_sec_provider
# openthread
# perfmon
# protobuf-c
# protocomm
# tcp_transport
# touch_element
# ulp
# unity
# usb
# wear_levelling
# wifi_provisioning
# wpa_supplicant

set(COMPONENTS
    __pio_env
    bootloader
    bootloader_support
    console
    cxx
    efuse
    esp_app_format
    esp_bootloader_format
    esp_common
    esp_driver_gpio
    esp_driver_gptimer
    esp_driver_rmt
    esp_driver_uart
    esp_driver_usb_serial_jtag
    esp_event
    esp_gdbstub
    esp_hw_support
    esp_mm
    esp_partition
    esp_ringbuf
    esp_rom
    esp_system
    esp_timer
    esp_vfs_console
    esptool_py
    freertos
    hal
    heap
    log
    newlib
    nvs_flash
    partition_table
    pthread
    rt
    soc
    spi_flash
	spiffs
    src
    vfs
    xtensa
    # BUG: https://github.com/espressif/esp-idf/issues/18072?utm_source=chatgpt.com
    mbedtls
)

if(ENABLE_PSRAM)
    list(APPEND COMPONENTS esp_psram)
endif()

if(ENABLE_I2C)
    list(APPEND COMPONENTS esp_driver_i2c)
endif()

if(ENABLE_I2S)
    list(APPEND COMPONENTS esp_driver_i2s)
endif()

if(ENABLE_SPI)
    list(APPEND COMPONENTS esp_driver_spi)
endif()

if(ENABLE_LEDC)
    list(APPEND COMPONENTS esp_driver_ledc)
endif()

if(ENABLE_ADC)
    list(APPEND COMPONENTS esp_adc)
endif()

if(ENABLE_TEMP_SENSOR)
    list(APPEND COMPONENTS esp_driver_tsens)
endif()

if(ENABLE_TWAI)
    list(APPEND COMPONENTS esp_driver_twai)
endif()

if(ENABLE_SDMMC)
    list(APPEND COMPONENTS
        esp_driver_sdmmc
        sdmmc
    )
endif()

if (ENABLE_WIFI)
    list(APPEND COMPONENTS
        esp_wifi
        esp_phy
        esp_netif
        esp_netif_stack
        lwip
        esp_coex
    )
endif()

if (ENABLE_BLUETOOTH)
    list(APPEND COMPONENTS
        bt
        esp_phy
        esp_coex
        lwip
    )
endif()

if (ENABLE_BTSTACK)
    list(APPEND COMPONENTS
        btstack
        btstack_config
    )
endif()

if (ENABLE_HTTPD)
    list(APPEND COMPONENTS esp_http_server)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(DEPENDENCIES_LOCK "${CMAKE_BINARY_DIR}/dependencies.lock" CACHE FILEPATH
  "ESP-IDF component manager lock file" FORCE)
idf_build_set_property(DEPENDENCIES_LOCK "${DEPENDENCIES_LOCK}")

# PlatformIO builds every environment from the same source tree. Keep
# ESP-IDF component-manager state in the per-environment build directory so
# different IDF targets do not rewrite one root dependencies.lock and
# managed_components directory.
macro(idf_build_process target)
  set(_spi_idf_build_args ${ARGN})
  list(FIND _spi_idf_build_args PROJECT_DIR _spi_project_dir_index)
  if(NOT _spi_project_dir_index EQUAL -1)
    math(EXPR _spi_project_dir_value_index "${_spi_project_dir_index} + 1")
    list(REMOVE_AT _spi_idf_build_args ${_spi_project_dir_value_index})
    list(REMOVE_AT _spi_idf_build_args ${_spi_project_dir_index})
  endif()

  _idf_build_process(${target}
    ${_spi_idf_build_args}
    PROJECT_DIR "${CMAKE_BINARY_DIR}"
  )
endmacro()
project(spi)
