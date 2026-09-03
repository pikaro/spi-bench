#include "LedDisplay/Outputs/detail/platform/Sk9822SpiESP32.hpp"
#include "Platform/Hardware.hpp"
#include "Types/Error.hpp"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace {

using Sk9822Transport =
    Totem::LedDisplay::Outputs::detail::platform::Sk9822SpiESP32;

constexpr char logTag[] = "led_scratch";
constexpr size_t pixelCount = 960;
constexpr size_t startFrameBytes = 4;
constexpr size_t bytesPerPixel = 4;
constexpr size_t endFrameBytes = 4U * ((pixelCount / 32U) + 1U);
constexpr size_t frameBytes =
    startFrameBytes + (pixelCount * bytesPerPixel) + endFrameBytes;
constexpr std::byte pixelHeader{0xE1}; // Lowest nonzero hardware brightness.
constexpr std::byte red{0xFF};
constexpr gpio_num_t outputGatePin = GPIO_NUM_9;
constexpr uint32_t refreshIntervalMs = 1000;

alignas(4) std::array<std::byte, frameBytes> frame{};
Sk9822Transport transport{};

[[noreturn]] void abortOnError(const char *operation, ReturnCode error) {
    const auto view = error.format();
    ESP_LOGE(logTag, "transport_failure operation=%s error=%s/%s code=%u",
             operation, view.domain, view.name,
             static_cast<unsigned>(view.code));
    std::abort();
}

void requireOk(const char *operation, ReturnCode result) {
    if (!result.ok()) {
        abortOnError(operation, result);
    }
}

void prepareSolidRedFrame() {
    frame.fill(std::byte{0});
    size_t offset = startFrameBytes;
    for (size_t pixel = 0; pixel < pixelCount; ++pixel) {
        frame[offset++] = pixelHeader;
        frame[offset++] = std::byte{0}; // Blue
        frame[offset++] = std::byte{0}; // Green
        frame[offset++] = red;
    }
}

void configureOutputGateDisabled() {
    ESP_ERROR_CHECK(gpio_set_level(outputGatePin, 1));
    const gpio_config_t config{
        .pin_bit_mask = 1ULL << outputGatePin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_ERROR_CHECK(gpio_set_level(outputGatePin, 1));
}

} // namespace

extern "C" void app_main() {
    configureOutputGateDisabled();
    prepareSolidRedFrame();

    const Totem::LedDisplay::Sk9822OutputConfig outputConfig{
        .host = Totem::LedDisplay::Sk9822SpiHost::Spi3,
        .dataPin = Pin::GPIO13,
        .clockPin = Pin::GPIO10,
        .clockHz = 4'000'000,
        .transferTimeoutMs = 10,
        .colorOrder = Totem::LedDisplay::Sk9822WireColorOrder::Bgr,
    };
    requireOk("begin", transport.begin(outputConfig, frame));

    ESP_ERROR_CHECK(gpio_set_level(outputGatePin, 0));
    ESP_LOGI(logTag,
             "solid_frame_active pixels=%u bytes=%u color=red level=1 "
             "data_gpio=13 clock_gpio=10 gate_gpio=9 clock_hz=4000000",
             static_cast<unsigned>(pixelCount),
             static_cast<unsigned>(frameBytes));

    for (;;) {
        requireOk("transmit", transport.transmit(frame));
        vTaskDelay(pdMS_TO_TICKS(refreshIntervalMs));
    }
}
