if(DEFINED BTSTACK_ROOT AND IS_DIRECTORY "${BTSTACK_ROOT}")
  # Supplied by the top-level build.
elseif(DEFINED ENV{BTSTACK_ROOT} AND IS_DIRECTORY "$ENV{BTSTACK_ROOT}")
  set(BTSTACK_ROOT "$ENV{BTSTACK_ROOT}")
elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack")
  set(BTSTACK_ROOT "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/btstack")
elseif(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/BTstack")
  set(BTSTACK_ROOT "${CMAKE_SOURCE_DIR}/.pio/libdeps/media-btstack/BTstack")
else()
  message(FATAL_ERROR "BTSTACK_ROOT is not set and BTstack was not found in media-btstack libdeps")
endif()

set(BTSTACK_PORT_ROOT "${BTSTACK_ROOT}/port/esp32/components/btstack")

file(GLOB btstack_sbc_decoder_sources
  "${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/srce/*.c"
)

set(btstack_core_sources
  "${BTSTACK_ROOT}/src/ad_parser.c"
  "${BTSTACK_ROOT}/src/btstack_crypto.c"
  "${BTSTACK_ROOT}/src/btstack_linked_list.c"
  "${BTSTACK_ROOT}/src/btstack_linked_queue.c"
  "${BTSTACK_ROOT}/src/btstack_memory.c"
  "${BTSTACK_ROOT}/src/btstack_memory_pool.c"
  "${BTSTACK_ROOT}/src/btstack_ring_buffer.c"
  "${BTSTACK_ROOT}/src/btstack_run_loop.c"
  "${BTSTACK_ROOT}/src/btstack_tlv.c"
  "${BTSTACK_ROOT}/src/btstack_util.c"
  "${BTSTACK_ROOT}/src/hci.c"
  "${BTSTACK_ROOT}/src/hci_cmd.c"
  "${BTSTACK_ROOT}/src/hci_dump.c"
  "${BTSTACK_ROOT}/src/l2cap.c"
  "${BTSTACK_ROOT}/src/l2cap_signaling.c"
)

set(btstack_classic_sources
  "${BTSTACK_ROOT}/src/classic/a2dp.c"
  "${BTSTACK_ROOT}/src/classic/a2dp_sink.c"
  "${BTSTACK_ROOT}/src/classic/avdtp.c"
  "${BTSTACK_ROOT}/src/classic/avdtp_acceptor.c"
  "${BTSTACK_ROOT}/src/classic/avdtp_initiator.c"
  "${BTSTACK_ROOT}/src/classic/avdtp_sink.c"
  "${BTSTACK_ROOT}/src/classic/avdtp_util.c"
  "${BTSTACK_ROOT}/src/classic/btstack_link_key_db_tlv.c"
  "${BTSTACK_ROOT}/src/classic/btstack_sbc_decoder_bluedroid.c"
  "${BTSTACK_ROOT}/src/classic/btstack_sbc_plc.c"
  "${BTSTACK_ROOT}/src/classic/device_id_server.c"
  "${BTSTACK_ROOT}/src/classic/sdp_client.c"
  "${BTSTACK_ROOT}/src/classic/sdp_server.c"
  "${BTSTACK_ROOT}/src/classic/sdp_util.c"
)

set(btstack_port_sources
  "${BTSTACK_PORT_ROOT}/btstack_port_esp32.c"
  "${BTSTACK_PORT_ROOT}/btstack_stdio_esp32.c"
  "${BTSTACK_PORT_ROOT}/btstack_tlv_esp32.c"
  "${BTSTACK_ROOT}/platform/freertos/btstack_run_loop_freertos.c"
  "${BTSTACK_ROOT}/3rd-party/md5/md5.c"
)

idf_component_register(
  SRCS
    ${btstack_core_sources}
    ${btstack_classic_sources}
    ${btstack_sbc_decoder_sources}
    ${btstack_port_sources}
  INCLUDE_DIRS
    "${BTSTACK_ROOT}/3rd-party/bluedroid/decoder/include"
    "${BTSTACK_ROOT}/3rd-party/bluedroid/encoder/include"
    "${BTSTACK_ROOT}/3rd-party/md5"
    "${BTSTACK_ROOT}/src"
    "${BTSTACK_ROOT}/src/classic"
    "${BTSTACK_ROOT}/platform/embedded"
    "${BTSTACK_ROOT}/platform/freertos"
    "${BTSTACK_PORT_ROOT}/include"
  PRIV_REQUIRES
    bt
    esp_driver_uart
    esp_driver_usb_serial_jtag
    esp_timer
    lwip
    nvs_flash
    vfs
  REQUIRES
    btstack_config
)
