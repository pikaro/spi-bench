#include "StatusLed/Interfaces/Config.hpp"
#include "StatusLed/Interfaces/Types.hpp"
#include <iostream>

namespace {

using Totem::StatusLed::BrightnessMultiplier;
using Totem::StatusLed::Config;
using Totem::StatusLed::RgbColor;

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void testDefaultBrightness() {
    constexpr Config config{};
    expect(config.brightness.percent == 30,
           "status LED brightness must default to 30 percent");
}

void testBrightnessScaling() {
    constexpr RgbColor color{.red = 255, .green = 160, .blue = 1};

    constexpr auto off = BrightnessMultiplier::fromPercent(0).apply(color);
    expect(off == RgbColor{}, "zero percent must clear every channel");

    constexpr auto full = BrightnessMultiplier::fromPercent(100).apply(color);
    expect(full == color, "100 percent must preserve every channel");

    constexpr auto dimmed = BrightnessMultiplier::fromPercent(30).apply(color);
    expect(dimmed == RgbColor{.red = 76, .green = 48, .blue = 0},
           "30 percent must multiply and truncate every channel");
}

void testBrightnessValidation() {
    constexpr Config valid{
        .configured = true,
        .brightness = BrightnessMultiplier::fromPercent(100),
    };
    expect(valid.validate(), "100 percent brightness must be valid");

    constexpr Config invalid{
        .configured = true,
        .brightness = BrightnessMultiplier::fromPercent(101),
    };
    expect(!invalid.validate(), "brightness above 100 percent must be invalid");
}

} // namespace

int main() {
    testDefaultBrightness();
    testBrightnessScaling();
    testBrightnessValidation();

    if (failures != 0) {
        std::cerr << failures << " status LED test(s) failed\n";
        return 1;
    }

    std::cout << "Status LED logic tests passed\n";
    return 0;
}
