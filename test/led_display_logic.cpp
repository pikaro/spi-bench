#include "LedDisplay/Animations/RadialGauge/Animation.hpp"
#include "LedDisplay/Animations/RadialMenu/Animation.hpp"
#include "LedDisplay/Outputs/detail/Sk9822Encoder.hpp"
#include "LedDisplay/Primitives/Canvas.hpp"
#include "LedDisplay/detail/LayerStack.hpp"
#include "LedTopology/Facade.hpp"
#include "StaticConfig/LedDisplay.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using Totem::LedDisplay::HsvColor;
using Totem::LedDisplay::Layer;
using Totem::LedDisplay::Sk9822WireColorOrder;
using Totem::LedDisplay::Outputs::detail::Sk9822Encoder;
using Totem::LedDisplay::Outputs::detail::Sk9822EncodeResult;
using Totem::LedTopology::DenseUmbrella;
using Totem::LedTopology::OwnedPixels;
using Totem::LedTopology::PhysicalPixelIndex;
using Totem::LedTopology::Umbrella;

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <typename Topology> void testTopologyBijection() {
    std::array<bool, Topology::totalPixelCount> seen{};
    for (size_t spoke = 0; spoke < Topology::spokeCount; ++spoke) {
        for (size_t radial = 0; radial < Topology::ringCount; ++radial) {
            const auto physical = Topology::physicalFor(
                static_cast<uint8_t>(spoke), static_cast<uint8_t>(radial));
            expect(static_cast<size_t>(physical) < seen.size(),
                   "topology physical index must be in range");
            if (static_cast<size_t>(physical) >= seen.size()) {
                continue;
            }
            expect(!seen[physical], "topology map must be bijective");
            seen[physical] = true;
            expect(Topology::radialForPhysical(physical) == radial,
                   "topology radial inverse must round-trip");
        }
    }
    for (const auto visited : seen) {
        expect(visited, "topology map must visit every physical pixel");
    }
}

void testDenseUmbrellaEndpoints() {
    expect(DenseUmbrella::physicalFor(0, 0) == 0,
           "dense spoke 0 must start at physical 0");
    expect(DenseUmbrella::physicalFor(0, 59) == 59,
           "dense spoke 0 must end at physical 59");
    expect(DenseUmbrella::physicalFor(1, 0) == 119,
           "dense spoke 1 center must be physical 119");
    expect(DenseUmbrella::physicalFor(1, 59) == 60,
           "dense spoke 1 outer pixel must be physical 60");
    expect(DenseUmbrella::physicalFor(15, 0) == 959,
           "GPU0 final spoke center must be physical 959");
    expect(DenseUmbrella::physicalFor(16, 0) == 960,
           "GPU1 first spoke center must be physical 960");
    expect(DenseUmbrella::physicalFor(31, 0) == 1919,
           "GPU1 final spoke center must be physical 1919");
}

void testSelectedOwnership() {
    size_t ownedCount = 0;
    for (size_t physical = 0; physical < LedDisplayConfig::totalPixelCount;
         ++physical) {
        const auto pixel = static_cast<PhysicalPixelIndex>(physical);
        const auto group = physical / LedDisplayConfig::groupPixelCount;
        bool expectedOwned = false;
        for (const auto configuredGroup : LedDisplayConfig::nodeGroups) {
            if (configuredGroup == group) {
                expectedOwned = true;
                break;
            }
        }
        expect(OwnedPixels::owns(pixel) == expectedOwned,
               "selected ownership must match configured groups");
        if (!expectedOwned) {
            continue;
        }
        ++ownedCount;
        const auto local = OwnedPixels::localIndex(pixel);
        expect(static_cast<size_t>(local) < LedDisplayConfig::ownedPixelCount,
               "owned local index must be in range");
        expect(OwnedPixels::physicalIndex(local) == pixel,
               "owned physical/local mapping must round-trip");
    }
    expect(ownedCount == LedDisplayConfig::ownedPixelCount,
           "owned physical count must match static configuration");

    for (size_t local = 0; local < LedDisplayConfig::ownedPixelCount; ++local) {
        const auto pixel = OwnedPixels::physicalIndex(
            static_cast<Totem::LedTopology::LocalPixelIndex>(local));
        expect(OwnedPixels::owns(pixel),
               "every local pixel must resolve to an owned physical pixel");
        expect(OwnedPixels::localIndex(pixel) == local,
               "local/physical mapping must round-trip");
    }
}

void testSelectedProfile() {
#if LED_TOPOLOGY_DENSE_UMBRELLA
    expect(LedDisplayConfig::spokeCount == 32,
           "dense profile must select 32 spokes");
    expect(LedDisplayConfig::ringCount == 60,
           "dense profile must select 60 rings");
    expect(LedDisplayConfig::centerGapDiameterMm == 120,
           "dense profile must select the 120 mm center gap");
    expect(LedDisplayConfig::radialStripLengthMm == 417,
           "dense profile must derive the 60-pixel strip length at 144/m");
    expect(LedDisplayConfig::innerRadiusMm == 60,
           "dense profile must select the 60 mm inner radius");
    expect(LedDisplayConfig::outerRadiusMm == 477,
           "dense profile must derive the outer radius");
#else
    expect(LedDisplayConfig::spokeCount == 16,
           "legacy profile must select 16 spokes");
    expect(LedDisplayConfig::ringCount == 46,
           "legacy profile must select 46 rings");
    expect(LedDisplayConfig::centerGapDiameterMm == 300,
           "legacy profile must retain the 300 mm center gap");
    expect(LedDisplayConfig::radialStripLengthMm == 300,
           "legacy profile must retain the 300 mm strip length");
#endif
}

void testRadialGaugeGeometry() {
    constexpr auto logicalToLocal =
        Totem::LedDisplay::Primitives::identityLogicalToLocalMap();
    std::array<HsvColor, LedDisplayConfig::totalPixelCount> frame{};
    Totem::LedDisplay::AnimationInputSnapshot inputs{};

    const auto render =
        [&](Totem::LedDisplay::Animations::RadialGaugeConfig config) {
            frame.fill({});
            auto canvas =
                Totem::LedDisplay::Primitives::Canvas{frame, logicalToLocal};
            auto context = Totem::LedDisplay::AnimationRenderContext{
                .canvas = canvas,
                .inputs = inputs,
            };
            Totem::LedDisplay::Animations::RadialGauge{.config = config}.render(
                context);
        };

    render({.value = 0, .maximumValue = 31});
    size_t litPixels = 0;
    for (uint8_t spoke = 0; spoke < LedDisplayConfig::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < LedDisplayConfig::ringCount;
             ++radial) {
            const auto pixel =
                frame[Totem::LedDisplay::Primitives::Canvas::logicalIndex(
                    spoke, radial)];
            if (pixel.value == 0) {
                continue;
            }
            ++litPixels;
            expect(radial == LedDisplayConfig::ringCount - 1U,
                   "default gauge must only use the outermost ring");
            expect(pixel.saturation == 0 && pixel.value == 255,
                   "default gauge pixels must be full white");
        }
    }
    expect(litPixels == 1,
           "brightness level zero must light the first of 32 states");

    render({.value = 31, .maximumValue = 31});
    litPixels = 0;
    for (const auto &pixel : frame) {
        if (pixel.value != 0) {
            ++litPixels;
        }
    }
    expect(litPixels == LedDisplayConfig::spokeCount,
           "maximum brightness must fill the complete outer ring");

    render({
        .value = 100,
        .maximumValue = 100,
        .startHue = 0,
        .startSaturation = 255,
        .startValue = 255,
        .endHue = 96,
        .endSaturation = 255,
        .endValue = 255,
        .centerRing = 20,
        .ringWidth = 3,
    });
    litPixels = 0;
    for (uint8_t spoke = 0; spoke < LedDisplayConfig::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < LedDisplayConfig::ringCount;
             ++radial) {
            const auto pixel =
                frame[Totem::LedDisplay::Primitives::Canvas::logicalIndex(
                    spoke, radial)];
            if (pixel.value == 0) {
                continue;
            }
            ++litPixels;
            expect(radial >= 19 && radial <= 21,
                   "configured gauge width must be centered on its ring");
            if (spoke == 0) {
                expect(pixel.hue == 0,
                       "gauge spectrum must start at its first color");
            }
            if (spoke == LedDisplayConfig::spokeCount - 1U) {
                expect(pixel.hue == 96,
                       "gauge spectrum must end at its second color");
            }
        }
    }
    expect(litPixels == LedDisplayConfig::spokeCount * 3U,
           "three-ring gauge must fill all configured band pixels");

    render({
        .value = 31,
        .maximumValue = 31,
        .centerRing =
            Totem::LedDisplay::Animations::RadialGaugeSpec::outermostRing,
        .ringWidth = 3,
    });
    litPixels = 0;
    for (uint8_t spoke = 0; spoke < LedDisplayConfig::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < LedDisplayConfig::ringCount;
             ++radial) {
            const auto pixel =
                frame[Totem::LedDisplay::Primitives::Canvas::logicalIndex(
                    spoke, radial)];
            if (pixel.value == 0) {
                continue;
            }
            ++litPixels;
            expect(radial >= LedDisplayConfig::ringCount - 3U,
                   "outer gauge width must shift inward without clipping");
        }
    }
    expect(litPixels == LedDisplayConfig::spokeCount * 3U,
           "outer gauge must retain its requested width");
}

void testRadialMenuGeometry() {
    constexpr auto logicalToLocal =
        Totem::LedDisplay::Primitives::identityLogicalToLocalMap();
    std::array<HsvColor, LedDisplayConfig::totalPixelCount> frame{};
    Totem::LedDisplay::AnimationInputSnapshot inputs{};
    auto config = Totem::LedDisplay::Animations::RadialMenuConfig{};
    config.itemCount = static_cast<uint8_t>(std::min<size_t>(
        Totem::LedDisplay::Animations::RadialMenuSpec::maximumItems,
        LedDisplayConfig::spokeCount / 4U));
    config.selectedItem = 0;
    config.populatedItems = 0x01;
    config.unfurlDurationMs = 100;

    const auto render = [&](uint32_t elapsedMs) {
        frame.fill({});
        auto canvas =
            Totem::LedDisplay::Primitives::Canvas{frame, logicalToLocal};
        auto context = Totem::LedDisplay::AnimationRenderContext{
            .clock = {.elapsedMs = elapsedMs},
            .canvas = canvas,
            .inputs = inputs,
        };
        Totem::LedDisplay::Animations::RadialMenu{.config = config}.render(
            context);
    };
    const auto pixel = [&](uint8_t spoke, uint8_t ring) {
        return frame[Totem::LedDisplay::Primitives::Canvas::logicalIndex(
            spoke, ring)];
    };

    render(0);
    expect(pixel(0, 0).saturation == 255 && pixel(0, 0).value == 255,
           "folded menu bar must use the populated item color");
    expect(pixel(3, 1).value == 255,
           "folded menu bar must cover four spokes by two rings");
    expect(pixel(1, 3).value == 255 && pixel(2, 3).value == 255,
           "folded menu tip must extend its center two spokes by two rings");
    expect(pixel(0, 2).value == 0,
           "folded menu tip must not cover the outer spokes");
    expect(pixel(4, 0).saturation == 0 && pixel(4, 0).value == 32,
           "empty menu items must render as dim white");

    render(100);
    expect(pixel(0, 5).value == 255,
           "unfurled menu bar must extend through six rings");
    expect(pixel(1, 7).value == 255 && pixel(2, 7).value == 255,
           "unfurled menu tip must extend two additional rings");
    expect(pixel(0, 6).value == 0,
           "unfurled menu tip must retain its independent spoke width");

    config.unfurledSpokeWidth = 6;
    render(100);
    expect(pixel(static_cast<uint8_t>(LedDisplayConfig::spokeCount - 1U), 5)
                   .value == 255 &&
               pixel(4, 5).value == 255,
           "unfurled menu width must expand independently of ring depth");
}

void testUiLayerPolicy() {
    using Totem::LedDisplay::AnimationStyle;
    using Totem::LedDisplay::BlendOp;
    using Totem::LedDisplay::detail::LayerStack;

    constexpr auto configs = Totem::LedDisplay::detail::defaultLayerConfigs();
    constexpr auto uiIndex = Totem::LedDisplay::detail::layerIndex(Layer::UI);
    expect(uiIndex + 1U == LayerStack::layerCount,
           "UI must be the topmost composed layer");
    expect(configs[uiIndex].decay == 1 &&
               !configs[uiIndex].clearEachFrame &&
               configs[uiIndex].clearOnPlay && configs[uiIndex].enabled,
           "UI must retain frames with the two-second decay policy");
    expect(configs[uiIndex].style.blendOp == BlendOp::Replace &&
               configs[uiIndex].style.opacity == 255,
           "UI must replace lower layers at full opacity");

    LayerStack layers{};
    std::array<HsvColor, LedDisplayConfig::ownedPixelCount> composed{};
    layers.beginFrame(0);
    layers.scratch()[0] =
        HsvColor{.hue = 64, .saturation = 255, .value = 200};
    layers.blendScratch(
        Layer::Debug,
        AnimationStyle{.blendOp = BlendOp::Replace, .opacity = 255});
    layers.clearScratch();
    layers.scratch()[0] =
        HsvColor{.hue = 0, .saturation = 0, .value = 255};
    layers.blendScratch(
        Layer::UI,
        AnimationStyle{.blendOp = BlendOp::Replace, .opacity = 255});
    layers.compose(composed);
    expect(composed[0].saturation == 0 && composed[0].value == 255,
           "UI pixels must draw over lower layers");

    layers.beginFrame(1);
    layers.compose(composed);
    expect(composed[0].saturation == 0 && composed[0].value == 254,
           "UI pixels must begin decaying after animation output stops");

    layers.prepareForPlay(Layer::UI);
    layers.compose(composed);
    expect(composed[0].saturation == 255 && composed[0].value == 192,
           "a new UI animation must reveal the lower layer instead of old UI "
           "output");

    layers.clearScratch();
    layers.scratch()[0] =
        HsvColor{.hue = 0, .saturation = 0, .value = 255};
    layers.blendScratch(
        Layer::UI,
        AnimationStyle{.blendOp = BlendOp::Replace, .opacity = 255});
    for (uint16_t frame = 2; frame <= 256; ++frame) {
        layers.beginFrame(frame);
    }
    layers.compose(composed);
    expect(composed[0].value == 0,
           "UI pixels must fully decay after 255 frames");
}

[[nodiscard]] uint8_t byte(std::span<const std::byte> bytes, size_t index) {
    return std::to_integer<uint8_t>(bytes[index]);
}

void testSk9822BrightnessMapping() {
    for (uint16_t brightness = 0; brightness <= 255; ++brightness) {
        auto expected = static_cast<uint8_t>((brightness * 31U) / 255U);
        if (brightness != 0 && expected == 0) {
            expected = 1;
        }
        expect(Sk9822Encoder::brightnessLevel(
                   static_cast<uint8_t>(brightness)) == expected,
               "SK9822 brightness mapping must match settled policy");
    }
    expect(Sk9822Encoder::brightnessLevel(0) == 0,
           "zero brightness must encode hardware off");
    expect(Sk9822Encoder::brightnessLevel(1) == 1,
           "first nonzero brightness must encode level one");
    expect(Sk9822Encoder::brightnessLevel(16) == 1,
           "logical brightness 16 must remain level one");
    expect(Sk9822Encoder::brightnessLevel(17) == 2,
           "logical brightness 17 must enter level two");
    expect(Sk9822Encoder::brightnessLevel(255) == 31,
           "full brightness must encode level 31");
}

void testSk9822EmittedBrightnessHeaders() {
    constexpr std::array<HsvColor, 1> frame{
        HsvColor{.hue = 0, .saturation = 0, .value = 200},
    };
    std::array<std::byte, Sk9822Encoder::encodedSize(frame.size())> output{};

    for (uint16_t brightness = 0; brightness <= 255; ++brightness) {
        const auto value = static_cast<uint8_t>(brightness);
        expect(Sk9822Encoder::encode(frame, value, 0, 0,
                                     Sk9822WireColorOrder::Rgb,
                                     output) == Sk9822EncodeResult::Ok,
               "every logical brightness must encode");
        const auto level = Sk9822Encoder::brightnessLevel(value);
        expect(byte(output, 4) == static_cast<uint8_t>(0xE0U | level),
               "pixel header must contain the mapped hardware level");
        expect((byte(output, 4) & 0xE0U) == 0xE0U,
               "pixel header prefix must always be 111");
        const auto expectedChannel = value == 0 ? 0 : 200;
        expect(byte(output, 5) == expectedChannel &&
                   byte(output, 6) == expectedChannel &&
                   byte(output, 7) == expectedChannel,
               "hardware brightness must not pre-scale RGB bytes");
    }
}

void testSk9822AllColorOrdersAndRenderer() {
    constexpr HsvColor hsv{.hue = 37, .saturation = 211, .value = 193};
    const auto rgb = Totem::LedDisplay::detail::Render::hsvToRgb(hsv);
    expect(rgb.red != rgb.green && rgb.red != rgb.blue && rgb.green != rgb.blue,
           "wire-order fixture must have three distinct channels");

    struct OrderCase {
        Sk9822WireColorOrder order;
        std::array<uint8_t, 3> expected;
    };
    const std::array<OrderCase, 6> cases{{
        {Sk9822WireColorOrder::Rgb, {rgb.red, rgb.green, rgb.blue}},
        {Sk9822WireColorOrder::Rbg, {rgb.red, rgb.blue, rgb.green}},
        {Sk9822WireColorOrder::Grb, {rgb.green, rgb.red, rgb.blue}},
        {Sk9822WireColorOrder::Gbr, {rgb.green, rgb.blue, rgb.red}},
        {Sk9822WireColorOrder::Brg, {rgb.blue, rgb.red, rgb.green}},
        {Sk9822WireColorOrder::Bgr, {rgb.blue, rgb.green, rgb.red}},
    }};
    constexpr std::array<HsvColor, 1> frame{hsv};
    std::array<std::byte, Sk9822Encoder::encodedSize(frame.size())> output{};

    for (const auto &testCase : cases) {
        expect(Sk9822Encoder::encode(frame, 255, 0, 0, testCase.order,
                                     output) == Sk9822EncodeResult::Ok,
               "every SK9822 wire color order must encode");
        expect(byte(output, 5) == testCase.expected[0] &&
                   byte(output, 6) == testCase.expected[1] &&
                   byte(output, 7) == testCase.expected[2],
               "wire bytes must match the renderer and selected color order");
    }
}

void testSk9822FramingAndOrder() {
    constexpr std::array<HsvColor, 3> frame{
        HsvColor{.hue = 0, .saturation = 255, .value = 255},
        HsvColor{.hue = 86, .saturation = 255, .value = 255},
        HsvColor{.hue = 172, .saturation = 255, .value = 255},
    };
    std::array<std::byte, Sk9822Encoder::encodedSize(frame.size())> output{};
    const auto ret = Sk9822Encoder::encode(frame, 255, 0, 0,
                                           Sk9822WireColorOrder::Bgr, output);
    expect(ret == Sk9822EncodeResult::Ok,
           "SK9822 encoder must accept an exact output buffer");
    for (size_t index = 0; index < Sk9822Encoder::startFrameBytes; ++index) {
        expect(byte(output, index) == 0,
               "SK9822 start frame bytes must be zero");
    }

    expect(byte(output, 4) == 0xFF, "red header must contain level 31");
    expect(byte(output, 5) == 0 && byte(output, 6) == 0 &&
               byte(output, 7) == 255,
           "BGR order must place red in the third wire byte");
    expect(byte(output, 8) == 0xFF, "green header must contain level 31");
    expect(byte(output, 9) == 0 && byte(output, 10) == 255 &&
               byte(output, 11) == 0,
           "BGR order must place green in the second wire byte");
    expect(byte(output, 12) == 0xFF, "blue header must contain level 31");
    expect(byte(output, 13) == 255 && byte(output, 14) == 0 &&
               byte(output, 15) == 0,
           "BGR order must place blue in the first wire byte");
    for (size_t index = 16; index < output.size(); ++index) {
        expect(byte(output, index) == 0,
               "SK9822 trailing frame bytes must be zero");
    }
}

void testSk9822SizesAndBlack() {
    expect(Sk9822Encoder::encodedSize(0) == 8,
           "zero-pixel compatibility frame must be eight bytes");
    expect(Sk9822Encoder::encodedSize(1) == 12,
           "one-pixel frame must be twelve bytes");
    expect(Sk9822Encoder::encodedSize(31) == 132,
           "31-pixel frame size must match FastLED framing");
    expect(Sk9822Encoder::encodedSize(32) == 140,
           "32-pixel frame must add another end dword");
    expect(Sk9822Encoder::encodedSize(960) == 3968,
           "production half-frame must be 3,968 bytes");

    std::array<std::byte, Sk9822Encoder::encodedSize(2)> black{};
    expect(Sk9822Encoder::encodeBlack(2, black) == Sk9822EncodeResult::Ok,
           "black encoder must accept exact storage");
    expect(byte(black, 4) == 0xE0 && byte(black, 8) == 0xE0,
           "black frame must contain valid level-zero pixel headers");
    for (const auto index : {5U, 6U, 7U, 9U, 10U, 11U}) {
        expect(byte(black, index) == 0, "black frame RGB bytes must be zero");
    }

    std::array<std::byte, Sk9822Encoder::encodedSize(2) - 1> shortOutput{};
    std::array<std::byte, Sk9822Encoder::encodedSize(2) + 1> longOutput{};
    constexpr std::array<HsvColor, 2> frame{};
    expect(Sk9822Encoder::encode(frame, 255, 0, 0, Sk9822WireColorOrder::Bgr,
                                 shortOutput) ==
               Sk9822EncodeResult::InvalidSize,
           "encoder must reject incorrectly sized output storage");
    expect(Sk9822Encoder::encode(frame, 255, 0, 0, Sk9822WireColorOrder::Bgr,
                                 longOutput) == Sk9822EncodeResult::InvalidSize,
           "encoder must reject oversized output storage");
}

void testSk9822FloorsAndInputPreservation() {
    const std::array<HsvColor, 2> frame{
        HsvColor{.hue = 0, .saturation = 0, .value = 7},
        HsvColor{.hue = 0, .saturation = 0, .value = 255},
    };
    const auto original = frame;
    std::array<std::byte, Sk9822Encoder::encodedSize(frame.size())> output{};

    expect(Sk9822Encoder::encode(frame, 255, 0, 0, Sk9822WireColorOrder::Rgb,
                                 output) == Sk9822EncodeResult::Ok,
           "disabled floors must encode successfully");
    expect(byte(output, 5) == 7,
           "zero value floor must preserve a low nonzero channel");

    expect(Sk9822Encoder::encode(frame, 255, 10, 0, Sk9822WireColorOrder::Rgb,
                                 output) == Sk9822EncodeResult::Ok,
           "enabled value floor must encode successfully");
    expect(byte(output, 5) == 0 && byte(output, 6) == 0 && byte(output, 7) == 0,
           "enabled value floor must black pixels below its threshold");

    expect(Sk9822Encoder::encode(frame, 0, 0, 0, Sk9822WireColorOrder::Rgb,
                                 output) == Sk9822EncodeResult::Ok,
           "hardware-off frame must encode successfully");
    expect(byte(output, 4) == 0xE0 && byte(output, 5) == 0 &&
               byte(output, 6) == 0 && byte(output, 7) == 0,
           "hardware-off frame must also transmit black RGB bytes");

    const std::array<HsvColor, 2> lowLevelFrame{
        HsvColor{.hue = 0, .saturation = 0, .value = 254},
        HsvColor{.hue = 0, .saturation = 0, .value = 255},
    };
    expect(Sk9822Encoder::encode(lowLevelFrame, 16, 10, 0,
                                 Sk9822WireColorOrder::Rgb,
                                 output) == Sk9822EncodeResult::Ok,
           "level-one value-floor case must encode");
    expect(byte(output, 5) == 0 && byte(output, 9) == 255,
           "value floor must derive from the emitted hardware level");

    const std::array<HsvColor, 1> whiteFrame{
        HsvColor{.hue = 0, .saturation = 0, .value = 255},
    };
    std::array<std::byte, Sk9822Encoder::encodedSize(whiteFrame.size())>
        whiteOutput{};
    expect(Sk9822Encoder::encode(whiteFrame, 16, 0, 8,
                                 Sk9822WireColorOrder::Rgb,
                                 whiteOutput) == Sk9822EncodeResult::Ok,
           "level-one luma boundary must encode");
    expect(byte(whiteOutput, 5) == 255,
           "luma equal to the configured floor must remain visible");
    expect(Sk9822Encoder::encode(whiteFrame, 16, 0, 9,
                                 Sk9822WireColorOrder::Rgb,
                                 whiteOutput) == Sk9822EncodeResult::Ok,
           "level-one luma rejection must encode");
    expect(byte(whiteOutput, 5) == 0 && byte(whiteOutput, 6) == 0 &&
               byte(whiteOutput, 7) == 0,
           "luma floor must use the emitted hardware brightness level");

    for (size_t index = 0; index < frame.size(); ++index) {
        expect(frame[index].hue == original[index].hue &&
                   frame[index].saturation == original[index].saturation &&
                   frame[index].value == original[index].value,
               "SK9822 encoding must not mutate HSV input");
    }
}

} // namespace

int main() {
    testTopologyBijection<Umbrella>();
    testTopologyBijection<DenseUmbrella>();
    testDenseUmbrellaEndpoints();
    testSelectedProfile();
    testSelectedOwnership();
    testRadialGaugeGeometry();
    testRadialMenuGeometry();
    testUiLayerPolicy();
    testSk9822BrightnessMapping();
    testSk9822EmittedBrightnessHeaders();
    testSk9822AllColorOrdersAndRenderer();
    testSk9822FramingAndOrder();
    testSk9822SizesAndBlack();
    testSk9822FloorsAndInputPreservation();

    if (failures != 0) {
        std::cerr << failures << " LED display test(s) failed\n";
        return 1;
    }
    std::cout << "LED display logic tests passed\n";
    return 0;
}
