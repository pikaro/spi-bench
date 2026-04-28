#pragma once

#include "Macros/Facade.hpp"
#include "Types/Error.hpp"
#include "Types/Uart.hpp"
#include "driver/gpio.h" // IWYU pragma: keep
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/uart_types.h"
#include "soc/clk_tree_defs.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

namespace platform {

class Uart {
    struct CallbackRegistration {
        void *owner = nullptr;
        UartEventCallback callback = nullptr;
    };

  public:
    ReturnCode init(UartConfig config) {
        ABORT_IF(config.uartNumber > UART_NUM_MAX, "Invalid UART number");
        FAIL_IF(!config.validate(), ERR(InvalidArgument),
                "Invalid UART config");
        _config = config;

        const uart_config_t uart_config = {
            .baud_rate = static_cast<int>(_config.baudRate),
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT,
            .flags =
                {
                    .allow_pd = 0,
                    .backup_before_sleep = 0,
                },
        };

        FAIL_IF_ESP(uart_driver_install(_uartNumber(), 2048,
                                        static_cast<int>(_config.txBufferSize),
                                        eventQueueDepth, &_eventQueue, 0),
                    ERR(OperationFailed), "Failed to install UART driver");
        FAIL_IF_ESP(uart_param_config(_uartNumber(), &uart_config),
                    ERR(OperationFailed),
                    "Failed to configure UART parameters");
        FAIL_IF_ESP(uart_set_pin(_uartNumber(),
                                 _pinOrNoChange(_config.pins.txPin),
                                 _pinOrNoChange(_config.pins.rxPin),
                                 _pinOrNoChange(_config.pins.rtsPin),
                                 _pinOrNoChange(_config.pins.ctsPin)),
                    ERR(OperationFailed), "Failed to set UART pins");

        auto taskCreated =
            xTaskCreate(_eventTaskMain, "UartEventTask", eventTaskStackSize,
                        this, eventTaskPriority, &_eventTask);
        FAIL_IF(taskCreated != pdPASS, ERR(OperationFailed),
                "Failed to start UART event task");

        return OK();
    }

    ReturnCode registerCallback(void *owner, UartEventCallback callback) {
        FAIL_IF(owner == nullptr || callback == nullptr, ERR(InvalidArgument),
                "Invalid UART event callback registration");
        for (auto &registration : _callbacks) {
            if (registration.callback == nullptr) {
                registration = {
                    .owner = owner,
                    .callback = callback,
                };
                return OK();
            }
        }
        return ERR(OutOfMemory);
    }

    ReturnCode write(std::span<const std::byte> data, bool drain = false,
                     std::optional<uint32_t> timeoutMs = std::nullopt) {

        auto ret = uart_write_bytes(_uartNumber(),
                                    reinterpret_cast<const char *>(data.data()),
                                    data.size());

        FAIL_IF(ret < 0, ERR(OperationFailed), "Failed to write to UART");
        FAIL_IF(ret != static_cast<int>(data.size()), ERR(OperationFailed),
                "Incomplete UART write");

        if (drain) {
            FAIL_IF_ERR_FWD(waitTxComplete(timeoutMs.value_or(
                                _config.timeoutFromBytes(data.size()) * 2)),
                            "Failed to wait for UART transmission to complete");
        }

        return OK();
    }

    ReturnCode waitTxComplete(uint32_t timeoutMs) {
        size_t free;
        FAIL_IF_ESP(uart_get_tx_buffer_free_size(_uartNumber(), &free),
                    ERR(OperationFailed),
                    "Failed to get UART TX buffer free size");
        auto waitRet =
            uart_wait_tx_done(_uartNumber(), pdMS_TO_TICKS(timeoutMs));
        FAIL_IF_ESP(waitRet, ERR(OperationFailed), "Failed to flush UART");
        return OK();
    }

    std::expected<size_t, ReturnCode> available() const {
        size_t available = 0;
        FAIL_IF_ESP(uart_get_buffered_data_len(_uartNumber(), &available),
                    std::unexpected(ERR(OperationFailed)),
                    "Failed to get UART buffered data length");
        return available;
    }

    std::expected<size_t, ReturnCode>
    read(std::span<std::byte> buffer,
         std::optional<uint32_t> timeoutMs = std::nullopt) {
        if (buffer.empty()) {
            return std::unexpected(ERR(InvalidArgument));
        }

        size_t available = 0;
        FAIL_IF_ESP(uart_get_buffered_data_len(_uartNumber(), &available),
                    std::unexpected(ERR(OperationFailed)),
                    "Failed to get UART buffered data length");

        if (available == 0) {
            return std::unexpected(ERR(NotFound));
        }

        auto requested = std::min(available, buffer.size());

        auto ret =
            uart_read_bytes(_uartNumber(), buffer.data(), requested,
                            pdMS_TO_TICKS(timeoutMs.value_or(
                                _config.timeoutFromBytes(requested * 2))));

        FAIL_IF(ret < 0, std::unexpected(ERR(OperationFailed)),
                "Failed to read from UART");

        if (ret == 0) {
            return std::unexpected(ERR(NotFound));
        }

        return static_cast<size_t>(ret);
    }

    std::expected<size_t, ReturnCode> readExact(std::span<std::byte> buffer,
                                                uint32_t timeoutMs) {
        if (buffer.empty()) {
            return std::unexpected(ERR(InvalidArgument));
        }

        auto ret = uart_read_bytes(_uartNumber(), buffer.data(), buffer.size(),
                                   pdMS_TO_TICKS(timeoutMs));

        FAIL_IF(ret < 0, std::unexpected(ERR(OperationFailed)),
                "Failed to read from UART");

        if (ret == 0) {
            return std::unexpected(ERR(Timeout));
        }

        if (ret != static_cast<int>(buffer.size())) {
            return std::unexpected(ERR(Timeout));
        }

        return static_cast<size_t>(ret);
    }

    ReturnCode discardRx() {
        FAIL_IF_ESP(uart_flush_input(_uartNumber()), ERR(OperationFailed),
                    "Failed to flush UART input");
        return OK();
    }

    ReturnCode deinit() {
        if (_eventTask != nullptr) {
            auto *task = _eventTask;
            _eventTask = nullptr;
            vTaskDelete(task);
        }
        FAIL_IF_ESP(uart_driver_delete(_uartNumber()), ERR(OperationFailed),
                    "Failed to delete UART driver");
        _eventQueue = nullptr;
        return OK();
    }

  private:
    static void _eventTaskMain(void *arg) {
        auto *self = static_cast<Uart *>(arg);
        self->_eventTaskLoop();
    }

    void _eventTaskLoop() {
        uart_event_t event{};
        while (_eventTask != nullptr) {
            if (_eventQueue == nullptr) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            if (xQueueReceive(_eventQueue, &event, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            _dispatchEvent(_mapEvent(event));
        }
        vTaskDelete(nullptr);
    }

    void _dispatchEvent(UartEvent event) {
        for (auto &registration : _callbacks) {
            if (registration.callback != nullptr) {
                (void)registration.callback(registration.owner, event);
            }
        }
    }

    static UartEvent _mapEvent(const uart_event_t &event) {
        switch (event.type) {
        case UART_DATA:
            return {
                .type = UartEventType::Data,
                .size = event.size,
            };
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            return {
                .type = UartEventType::Overflow,
                .size = event.size,
            };
        case UART_BREAK:
            return {
                .type = UartEventType::Break,
                .size = event.size,
            };
        case UART_PARITY_ERR:
        case UART_FRAME_ERR:
            return {
                .type = UartEventType::Error,
                .size = event.size,
            };
        case UART_PATTERN_DET:
            return {
                .type = UartEventType::Pattern,
                .size = event.size,
            };
        default:
            return {
                .type = UartEventType::Unknown,
                .size = event.size,
            };
        }
    }

    UartConfig _config;
    QueueHandle_t _eventQueue = nullptr;
    TaskHandle_t _eventTask = nullptr;
    std::array<CallbackRegistration, 4> _callbacks{};

    static constexpr int eventQueueDepth = 16;
    static constexpr uint32_t eventTaskStackSize = 2048;
    static constexpr UBaseType_t eventTaskPriority = 3;

    static int _pinOrNoChange(std::optional<Pin> pin) {
        if (pin.has_value()) {
            return static_cast<int>(pin.value());
        }
        return UART_PIN_NO_CHANGE;
    }

    [[nodiscard]] uart_port_t _uartNumber() const {
        return static_cast<uart_port_t>(_config.uartNumber);
    }
};

} // namespace platform
