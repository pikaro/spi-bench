// IWYU pragma: private

#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Types/Uart.hpp"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_select.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <fcntl.h>
#include <optional>
#include <span>
#include <stdio.h>
#include <sys/_default_fcntl.h>
#include <sys/unistd.h>
#include <unistd.h>

namespace platform {

struct ConsoleCallbackRegistration {
    void *owner = nullptr;
    UartEventCallback callback = nullptr;
};

struct Console {
    static ReturnCode init() {
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        FAIL_IF_ERR_FWD(initUsbSerialJtagDriver(),
                        "Failed to initialize USB Serial/JTAG console");
#endif
        FAIL_IF_ERR_FWD(setNonblocking(fileno(stdin)),
                        "Failed to configure primary console input");
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
        if (secondaryFd < 0) {
            secondaryFd = ::open("/dev/secondary", O_RDWR | O_NONBLOCK);
            if (secondaryFd >= 0) {
                FAIL_IF_ERR_FWD(
                    setNonblocking(secondaryFd),
                    "Failed to configure secondary USB console input");
            }
        }
#endif
        return OK();
    }

    static ReturnCode registerCallback(void *owner,
                                       UartEventCallback callback) {
        FAIL_IF(owner == nullptr || callback == nullptr, ERR(InvalidArgument),
                "Invalid console event callback registration");
        for (auto &registration : callbacks) {
            if (registration.callback == nullptr) {
                registration = {
                    .owner = owner,
                    .callback = callback,
                };
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
                if (usb_serial_jtag_is_driver_installed() &&
                    usb_serial_jtag_read_ready()) {
                    notifyEventTask(notifyRead);
                }
#endif
                return OK();
            }
        }
        return ERR(OutOfMemory);
    }

    [[nodiscard]] static bool connected() {
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        return usb_serial_jtag_is_connected();
#else
        return true;
#endif
    }

    static ReturnCode write(std::span<const std::byte> data, bool drain = false,
                            std::optional<uint32_t> /*unused*/ = std::nullopt) {
        auto written = fwrite(reinterpret_cast<const char *>(data.data()), 1,
                              data.size(), stdout);
        FAIL_IF(written != data.size(), ERR(OperationFailed),
                "Failed to write to console");

        if (drain) {
            auto ret = fflush(stdout);
            FAIL_IF(ret != 0, ERR(OperationFailed), "Failed to flush console");
        }
        return OK();
    }

    static std::expected<size_t, ReturnCode>
    read(std::span<std::byte> buffer,
         std::optional<uint32_t> /*unused*/ = std::nullopt) {
        if (buffer.empty()) {
            return std::unexpected(ERR(InvalidArgument));
        }

        auto primary = readFd(fileno(stdin), buffer);
        if (primary) {
            notifyIfUsbSerialJtagReadReady();
            return primary;
        }
        if (primary.error() != ERR(NotFound)) {
            return primary;
        }

#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
        if (secondaryFd >= 0) {
            auto secondary = readFd(secondaryFd, buffer);
            if (secondary) {
                notifyIfUsbSerialJtagReadReady();
            }
            return secondary;
        }
#endif

        return std::unexpected(ERR(NotFound));
    }

    static ReturnCode deinit() {
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
        usb_serial_jtag_set_select_notif_callback(nullptr);
        if (eventTask != nullptr) {
            auto *task = eventTask;
            eventTask = nullptr;
            vTaskDelete(task);
        }
#endif
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
        if (secondaryFd >= 0) {
            (void)::close(secondaryFd);
            secondaryFd = -1;
        }
#endif
        return OK();
    }

  private:
#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    static ReturnCode initUsbSerialJtagDriver() {
        if (!usb_serial_jtag_is_driver_installed()) {
            auto config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
            config.tx_buffer_size = usbSerialJtagBufferSize;
            config.rx_buffer_size = usbSerialJtagBufferSize;
            FAIL_IF_PLATFORM_FWD(usb_serial_jtag_driver_install(&config),
                                 "Failed to install USB Serial/JTAG driver");
        }

        usb_serial_jtag_vfs_use_driver();
        if (eventTask == nullptr) {
            // USB Serial/JTAG notifications arrive from ISR context; keep
            // command wake callbacks in normal task context.
            auto created =
                xTaskCreate(eventTaskMain, "UsbConsoleEvt", eventTaskStackSize,
                            nullptr, eventTaskPriority, &eventTask);
            FAIL_IF(created != pdPASS || eventTask == nullptr,
                    ERR(OperationFailed),
                    "Failed to start USB Serial/JTAG console event task");
        }
        usb_serial_jtag_set_select_notif_callback(onUsbSerialJtagNotification);
        return OK();
    }

    static void onUsbSerialJtagNotification(usj_select_notif_t notification,
                                            BaseType_t *taskWoken) {
        uint32_t notificationBits = 0;
        switch (notification) {
        case USJ_SELECT_READ_NOTIF:
            notificationBits = notifyRead;
            break;
        case USJ_SELECT_ERROR_NOTIF:
            notificationBits = notifyError;
            break;
        case USJ_SELECT_WRITE_NOTIF:
            return;
        }

        if (eventTask == nullptr || notificationBits == 0) {
            return;
        }

        BaseType_t localTaskWoken = pdFALSE;
        auto *woken = taskWoken != nullptr ? taskWoken : &localTaskWoken;
        (void)xTaskNotifyFromISR(eventTask, notificationBits, eSetBits, woken);
    }

    static void eventTaskMain(void *) {
        while (eventTask != nullptr) {
            uint32_t notificationBits = 0;
            if (xTaskNotifyWait(0, UINT32_MAX, &notificationBits,
                                portMAX_DELAY) != pdTRUE) {
                continue;
            }

            if ((notificationBits & notifyRead) != 0) {
                dispatchEvent({
                    .type = UartEventType::Data,
                    .size = 0,
                });
            }
            if ((notificationBits & notifyError) != 0) {
                dispatchEvent({
                    .type = UartEventType::Error,
                    .size = 0,
                });
            }
        }
        vTaskDelete(nullptr);
    }

    static void notifyEventTask(uint32_t notificationBits) {
        if (eventTask != nullptr) {
            (void)xTaskNotify(eventTask, notificationBits, eSetBits);
        }
    }

    static void notifyIfUsbSerialJtagReadReady() {
        if (usb_serial_jtag_is_driver_installed() &&
            usb_serial_jtag_read_ready()) {
            notifyEventTask(notifyRead);
        }
    }
#else
    static void notifyIfUsbSerialJtagReadReady() {}
#endif

    static void dispatchEvent(UartEvent event) {
        for (auto &registration : callbacks) {
            if (registration.callback != nullptr) {
                REPORT_IF_ERR(
                    registration.callback(registration.owner, event),
                    "USB console event callback failed for event type %u",
                    static_cast<unsigned>(event.type));
            }
        }
    }

    static ReturnCode setNonblocking(int fd) {
        auto flags = fcntl(fd, F_GETFL, 0);
        FAIL_IF(flags < 0, ERR(OperationFailed),
                "Failed to get console input flags");
        FAIL_IF(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0,
                ERR(OperationFailed),
                "Failed to set console input nonblocking");
        return OK();
    }

    static std::expected<size_t, ReturnCode>
    readFd(int fd, std::span<std::byte> buffer) {
        auto ret = ::read(fd, reinterpret_cast<char *>(buffer.data()),
                          buffer.size());
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return std::unexpected(ERR(NotFound));
            }
            return std::unexpected(ERR(OperationFailed));
        }

        if (ret == 0) {
            return std::unexpected(ERR(NotFound));
        }

        return static_cast<size_t>(ret);
    }

#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    inline static int secondaryFd = -1;
#endif
    inline static std::array<ConsoleCallbackRegistration, 4> callbacks{};

#if CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG ||                             \
    CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    inline static TaskHandle_t eventTask = nullptr;
    static constexpr uint32_t notifyRead = 1U << 0;
    static constexpr uint32_t notifyError = 1U << 1;
    static constexpr uint32_t usbSerialJtagBufferSize = 2048;
    static constexpr uint32_t eventTaskStackSize = 2048;
    static constexpr UBaseType_t eventTaskPriority = 3;
#endif
};

} // namespace platform
