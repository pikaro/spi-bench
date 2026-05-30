#include "TraceFormat.hpp"
#include "generated/AnimationRegistry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Totem::LedDisplay::AnimationRenderContext;
using Totem::LedDisplay::Config;
using Totem::LedDisplay::FrameClock;
using Totem::LedDisplay::Layer;
using Totem::LedDisplay::detail::Compositor;
using Totem::LedDisplay::detail::LayerStack;
using Totem::LedRender::HsvColor;
using Totem::LedRender::JsonParser;
using Totem::LedRender::JsonValue;
using Totem::LedRender::RenderRequest;
using Totem::LedRender::RgbColor;

struct ActiveAnimation {
    Totem::LedRender::Generated::Payload payload;
    uint32_t startMs = 0;
    uint32_t durationMs = 0;
    Layer layer = Layer::Effect;
};

struct CliArgs {
    std::string configPath;
    std::string outputPath;
    std::string animationOverride;
    std::optional<std::pair<uint32_t, uint32_t>> frames;
    std::optional<uint32_t> fps;
    std::optional<bool> pipelineMode;
    bool includeScratch = false;
    bool includeScratchSet = false;
    bool list = false;
    bool help = false;
};

[[nodiscard]] std::string usage() {
    return R"(Usage:
  bin/led-render --config CONFIG.json --output TRACE.tled [options]
  bin/led-render --list

Options:
  --animation NAME       Override single-animation config animation name.
  --frames FIRST:LAST    Inclusive frame range. Defaults to 0:124.
  --fps FPS              Render frame rate. Defaults to firmware target FPS.
  --mode MODE            pipeline or animation. Defaults to pipeline.
  --include-scratch      Add the last rendered scratch frame as a trace plane.
  --help                 Show this help.
)";
}

[[nodiscard]] bool parseUint32(std::string_view text, uint32_t &out) {
    if (text.empty()) {
        return false;
    }
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10U) + static_cast<uint64_t>(c - '0');
        if (value > UINT32_MAX) {
            return false;
        }
    }
    out = static_cast<uint32_t>(value);
    return true;
}

[[nodiscard]] bool parseFrames(std::string_view text,
                               std::pair<uint32_t, uint32_t> &frames) {
    const auto colon = text.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    uint32_t first = 0;
    uint32_t last = 0;
    if (!parseUint32(text.substr(0, colon), first) ||
        !parseUint32(text.substr(colon + 1U), last) || last < first) {
        return false;
    }
    frames = {first, last};
    return true;
}

[[nodiscard]] bool parseMode(std::string_view text, bool &pipelineMode) {
    if (text == "pipeline") {
        pipelineMode = true;
        return true;
    }
    if (text == "animation") {
        pipelineMode = false;
        return true;
    }
    return false;
}

[[nodiscard]] bool parseArgs(int argc, char **argv, CliArgs &args,
                             std::string &error) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto requireValue = [&](std::string_view name) -> const char * {
            if (i + 1 >= argc) {
                error = "Missing value for " + std::string(name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else if (arg == "--list") {
            args.list = true;
        } else if (arg == "--config") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            args.configPath = value;
        } else if (arg == "--output") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            args.outputPath = value;
        } else if (arg == "--animation") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            args.animationOverride = value;
        } else if (arg == "--frames") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            std::pair<uint32_t, uint32_t> frames{};
            if (!parseFrames(value, frames)) {
                error = "Invalid --frames value, expected FIRST:LAST";
                return false;
            }
            args.frames = frames;
        } else if (arg == "--fps") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            uint32_t fps = 0;
            if (!parseUint32(value, fps) || fps == 0) {
                error = "Invalid --fps value";
                return false;
            }
            args.fps = fps;
        } else if (arg == "--mode") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            bool pipelineMode = true;
            if (!parseMode(value, pipelineMode)) {
                error = "Invalid --mode value, expected pipeline or animation";
                return false;
            }
            args.pipelineMode = pipelineMode;
        } else if (arg == "--include-scratch") {
            args.includeScratch = true;
            args.includeScratchSet = true;
        } else {
            error = "Unknown argument: " + std::string(arg);
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool readTextFile(const std::string &path, std::string &out,
                                std::string &error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Failed to open " + path;
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    out = stream.str();
    return true;
}

[[nodiscard]] bool readOptionalBool(const JsonValue &object,
                                    std::string_view key, bool &out,
                                    std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    return Totem::LedRender::readJsonBool(*value, out, error, key);
}

[[nodiscard]] bool readOptionalMode(const JsonValue &object,
                                    std::string_view key, bool &pipelineMode,
                                    std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    std::string mode;
    if (!Totem::LedRender::readJsonString(*value, mode, error, key)) {
        return false;
    }
    if (!parseMode(mode, pipelineMode)) {
        error = "Field '" + std::string(key) +
                "' must be 'pipeline' or 'animation'";
        return false;
    }
    return true;
}

[[nodiscard]] bool readOptionalFrames(const JsonValue &object,
                                      std::string_view key,
                                      uint32_t &firstFrame, uint32_t &lastFrame,
                                      std::string &error) {
    const auto *value = object.find(key);
    if (value == nullptr) {
        return true;
    }
    std::string frames;
    if (!Totem::LedRender::readJsonString(*value, frames, error, key)) {
        return false;
    }
    std::pair<uint32_t, uint32_t> parsed{};
    if (!parseFrames(frames, parsed)) {
        error = "Field '" + std::string(key) + "' must use FIRST:LAST syntax";
        return false;
    }
    firstFrame = parsed.first;
    lastFrame = parsed.second;
    return true;
}

[[nodiscard]] bool checkKnownKeys(const JsonValue &object,
                                  std::span<const std::string_view> known,
                                  std::string_view context,
                                  std::string &error) {
    for (const auto &[key, value] : object.object) {
        (void)value;
        bool found = false;
        for (const auto allowed : known) {
            if (key == allowed) {
                found = true;
                break;
            }
        }
        if (!found) {
            error = "Unknown " + std::string(context) + " field: " + key;
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool loadAnimationSpec(const JsonValue &object,
                                     std::string_view animationOverride,
                                     bool validateAnimationOnlyFields,
                                     ActiveAnimation &out, std::string &error) {
    if (object.kind != JsonValue::Kind::Object) {
        error = "Animation spec must be an object";
        return false;
    }

    constexpr std::array<std::string_view, 6> known{
        "animation", "config", "duration_ms", "start_ms", "layer", "comment",
    };
    if (validateAnimationOnlyFields &&
        !checkKnownKeys(object, known, "animation", error)) {
        return false;
    }

    std::string animationName;
    if (!animationOverride.empty()) {
        animationName = std::string(animationOverride);
    } else if (const auto *name = object.find("animation"); name != nullptr) {
        if (!Totem::LedRender::readJsonString(*name, animationName, error,
                                              "animation")) {
            return false;
        }
    } else {
        error = "Animation spec is missing 'animation'";
        return false;
    }

    auto payload = Totem::LedRender::Generated::makePayload(
        animationName, object.find("config"), error);
    if (!payload) {
        return false;
    }

    out.payload = *payload;
    out.durationMs =
        Totem::LedRender::Generated::defaultLifetimeMs(out.payload);
    out.layer = Totem::LedRender::Generated::defaultLayer(out.payload);

    return Totem::LedRender::readOptionalUint32(object, "start_ms", out.startMs,
                                                error) &&
           Totem::LedRender::readOptionalUint32(object, "duration_ms",
                                                out.durationMs, error) &&
           Totem::LedRender::readOptionalLayer(object, "layer", out.layer,
                                               error);
}

[[nodiscard]] bool loadRequest(const JsonValue &root, const CliArgs &cli,
                               RenderRequest &request,
                               std::vector<ActiveAnimation> &animations,
                               std::string &error) {
    if (root.kind != JsonValue::Kind::Object) {
        error = "Root JSON value must be an object";
        return false;
    }

    constexpr std::array<std::string_view, 17> known{
        "animation",       "animations", "config",          "duration_ms",
        "start_ms",        "layer",      "inputs",          "fps",
        "first_frame",     "last_frame", "frames",          "hue_offset",
        "rotation_offset", "mode",       "include_scratch", "comment",
        "metadata",
    };
    if (!checkKnownKeys(root, known, "root", error)) {
        return false;
    }

    if (!Totem::LedRender::readOptionalUint32(root, "fps", request.fps,
                                              error) ||
        !Totem::LedRender::readOptionalUint32(root, "first_frame",
                                              request.firstFrame, error) ||
        !Totem::LedRender::readOptionalUint32(root, "last_frame",
                                              request.lastFrame, error) ||
        !readOptionalFrames(root, "frames", request.firstFrame,
                            request.lastFrame, error) ||
        !Totem::LedRender::readOptionalUint8(root, "hue_offset",
                                             request.hueOffset, error) ||
        !Totem::LedRender::readOptionalUint8(root, "rotation_offset",
                                             request.rotationOffset, error) ||
        !readOptionalMode(root, "mode", request.pipelineMode, error) ||
        !readOptionalBool(root, "include_scratch", request.includeScratch,
                          error) ||
        !Totem::LedRender::readInputSnapshot(root, request.inputs, error)) {
        return false;
    }

    if (cli.fps) {
        request.fps = *cli.fps;
    }
    if (cli.frames) {
        request.firstFrame = cli.frames->first;
        request.lastFrame = cli.frames->second;
    }
    if (cli.pipelineMode) {
        request.pipelineMode = *cli.pipelineMode;
    }
    if (cli.includeScratchSet) {
        request.includeScratch = cli.includeScratch;
    }
    if (!cli.outputPath.empty()) {
        request.outputPath = cli.outputPath;
    }

    if (request.fps == 0) {
        error = "FPS must be greater than zero";
        return false;
    }
    if (request.lastFrame < request.firstFrame) {
        error = "last_frame must be >= first_frame";
        return false;
    }
    if (request.outputPath.empty()) {
        error = "--output is required";
        return false;
    }

    if (const auto *sequence = root.find("animations"); sequence != nullptr) {
        if (!cli.animationOverride.empty()) {
            error = "--animation cannot override a multi-animation sequence";
            return false;
        }
        if (sequence->kind != JsonValue::Kind::Array) {
            error = "Field 'animations' must be an array";
            return false;
        }
        for (const auto &entry : sequence->array) {
            ActiveAnimation animation{};
            if (!loadAnimationSpec(entry, {}, true, animation, error)) {
                return false;
            }
            animations.push_back(animation);
        }
    } else {
        ActiveAnimation animation{};
        if (!loadAnimationSpec(root, cli.animationOverride, false, animation,
                               error)) {
            return false;
        }
        animations.push_back(animation);
    }

    if (animations.empty()) {
        error = "No animations configured";
        return false;
    }
    return true;
}

[[nodiscard]] bool animationActive(const ActiveAnimation &animation,
                                   uint32_t nowMs) {
    if (nowMs < animation.startMs) {
        return false;
    }
    if (animation.durationMs == 0) {
        return true;
    }
    return (nowMs - animation.startMs) <= animation.durationMs;
}

void writeHsvPlane(std::ostream &out, std::span<const HsvColor> frame,
                   std::span<const Totem::LedTopology::LocalPixelIndex,
                             Config::totalPixelCount>
                       traceMap) {
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto logical =
                Totem::LedRender::Canvas::logicalIndex(spoke, radial);
            const auto local = traceMap[logical];
            HsvColor pixel{};
            if (static_cast<size_t>(local) < frame.size()) {
                pixel = frame[local];
            }
            out.write(reinterpret_cast<const char *>(&pixel.hue), 1);
            out.write(reinterpret_cast<const char *>(&pixel.saturation), 1);
            out.write(reinterpret_cast<const char *>(&pixel.value), 1);
        }
    }
}

void writeRgbPlane(std::ostream &out, std::span<const RgbColor> frame,
                   std::span<const Totem::LedTopology::LocalPixelIndex,
                             Config::totalPixelCount>
                       traceMap) {
    for (uint8_t spoke = 0; spoke < Config::spokeCount; ++spoke) {
        for (uint8_t radial = 0; radial < Config::ringCount; ++radial) {
            const auto logical =
                Totem::LedRender::Canvas::logicalIndex(spoke, radial);
            const auto local = traceMap[logical];
            RgbColor pixel{};
            if (static_cast<size_t>(local) < frame.size()) {
                pixel = frame[local];
            }
            out.write(reinterpret_cast<const char *>(&pixel.red), 1);
            out.write(reinterpret_cast<const char *>(&pixel.green), 1);
            out.write(reinterpret_cast<const char *>(&pixel.blue), 1);
        }
    }
}

[[nodiscard]] size_t planeCount(uint32_t mask) {
    size_t count = 0;
    for (uint32_t bit = 1U; bit != 0U; bit <<= 1U) {
        if ((mask & bit) != 0U) {
            ++count;
        }
        if (bit > Totem::LedRender::PlaneHsvScratch) {
            break;
        }
    }
    return count;
}

[[nodiscard]] std::string
metadataJson(const RenderRequest &request, const std::string &configPath,
             const std::vector<ActiveAnimation> &animations) {
    std::ostringstream out;
    out << "{";
    out << "\"format\":\"totem-led-trace\",";
    out << "\"format_version\":" << Totem::LedRender::traceVersion << ",";
    out << "\"renderer\":\"tools/led-render\",";
    out << "\"config_path\":\"" << Totem::LedRender::escapeJson(configPath)
        << "\",";
    out << "\"mode\":\"" << (request.pipelineMode ? "pipeline" : "animation")
        << "\",";
    out << "\"fps\":" << request.fps << ",";
    out << "\"first_frame\":" << request.firstFrame << ",";
    out << "\"last_frame\":" << request.lastFrame << ",";
    out << "\"hue_offset\":" << static_cast<unsigned>(request.hueOffset) << ",";
    out << "\"rotation_offset\":"
        << static_cast<unsigned>(request.rotationOffset) << ",";
    out << "\"color_backend\":\"GenericRenderer\",";
    out << "\"pixel_order\":\"logical_spoke_radial\",";
    out << "\"geometry\":{";
    out << "\"center_gap_diameter_mm\":"
        << static_cast<unsigned>(Config::centerGapDiameterMm) << ",";
    out << "\"radial_strip_length_mm\":"
        << static_cast<unsigned>(Config::radialStripLengthMm) << ",";
    out << "\"inner_radius_mm\":"
        << static_cast<unsigned>(Config::innerRadiusMm) << ",";
    out << "\"outer_radius_mm\":"
        << static_cast<unsigned>(Config::outerRadiusMm);
    out << "},";
    out << "\"animations\":[";
    for (size_t i = 0; i < animations.size(); ++i) {
        const auto &animation = animations[i];
        if (i != 0) {
            out << ",";
        }
        out << "{";
        out << "\"name\":\""
            << Totem::LedRender::escapeJson(
                   Totem::LedRender::Generated::name(animation.payload))
            << "\",";
        out << "\"start_ms\":" << animation.startMs << ",";
        out << "\"duration_ms\":" << animation.durationMs << ",";
        out << "\"layer\":\"" << Totem::LedRender::layerName(animation.layer)
            << "\"";
        out << "}";
    }
    out << "],";
    out << "\"source_json\":\""
        << Totem::LedRender::escapeJson(request.sourceJson) << "\"";
    out << "}";
    return out.str();
}

[[nodiscard]] bool renderTrace(const RenderRequest &request,
                               const std::string &configPath,
                               const std::vector<ActiveAnimation> &animations,
                               std::string &error) {
    std::ofstream out(request.outputPath, std::ios::binary);
    if (!out) {
        error = "Failed to open output trace " + request.outputPath;
        return false;
    }

    const auto renderMap =
        Totem::LedRender::buildLogicalToLocalMap(request.rotationOffset);
    const auto traceMap = Totem::LedRender::buildLogicalToLocalMap(0);

    uint32_t planeMask =
        Totem::LedRender::PlaneHsvFinal | Totem::LedRender::PlaneRgbFinal;
    if (request.includeScratch) {
        planeMask |= Totem::LedRender::PlaneHsvScratch;
    }

    const auto metadata = metadataJson(request, configPath, animations);
    Totem::LedRender::TraceHeader header{};
    std::memcpy(header.magic, Totem::LedRender::traceMagic,
                sizeof(header.magic));
    header.version = Totem::LedRender::traceVersion;
    header.headerSize = sizeof(Totem::LedRender::TraceHeader);
    header.flags = request.pipelineMode ? 1U : 0U;
    header.frameCount =
        Totem::LedRender::frameCount(request.firstFrame, request.lastFrame);
    header.firstFrame = request.firstFrame;
    header.fpsNum = request.fps;
    header.fpsDen = 1;
    header.frameStepUs = Totem::LedRender::frameStepUs(request.fps);
    header.stripCount = Config::stripCount;
    header.segmentsPerStrip = Config::segmentsPerStrip;
    header.spokeCount = Config::spokeCount;
    header.ringCount = Config::ringCount;
    header.pixelCount = Config::totalPixelCount;
    header.planeMask = planeMask;
    header.bytesPerFrame =
        static_cast<uint32_t>(planeCount(planeMask) * header.pixelCount * 3U);
    header.metadataBytes = static_cast<uint32_t>(metadata.size());

    out.write(reinterpret_cast<const char *>(&header), sizeof(header));
    out.write(metadata.data(), static_cast<std::streamsize>(metadata.size()));

    LayerStack layers{};
    std::array<HsvColor, Config::ownedPixelCount> finalHsv{};
    std::array<HsvColor, Config::ownedPixelCount> scratchTrace{};
    std::array<RgbColor, Config::ownedPixelCount> finalRgb{};

    for (uint32_t frameIndex = 0; frameIndex < header.frameCount;
         ++frameIndex) {
        const auto frame = request.firstFrame + frameIndex;
        const auto nowMs = Totem::LedRender::frameTimeMs(frame, request.fps);
        Compositor::clear(scratchTrace);

        if (request.pipelineMode) {
            layers.beginFrame(frame);
            for (const auto &animation : animations) {
                if (!animationActive(animation, nowMs)) {
                    continue;
                }
                const auto elapsedMs = nowMs - animation.startMs;
                layers.clearScratch();
                auto canvas =
                    Totem::LedRender::Canvas{layers.scratch(), renderMap};
                auto ctx = AnimationRenderContext{
                    .clock = FrameClock{.nowMs = nowMs,
                                        .elapsedMs = elapsedMs,
                                        .durationMs = animation.durationMs,
                                        .frame = frame},
                    .hueOffset = request.hueOffset,
                    .canvas = canvas,
                    .inputs = request.inputs,
                };
                Totem::LedRender::Generated::render(animation.payload, ctx);
                if (request.includeScratch) {
                    std::copy(layers.scratch().begin(), layers.scratch().end(),
                              scratchTrace.begin());
                }
                layers.blendScratch(
                    animation.layer,
                    Totem::LedRender::Generated::style(animation.payload));
            }
            layers.compose(finalHsv);
        } else {
            Compositor::clear(finalHsv);
            for (const auto &animation : animations) {
                if (!animationActive(animation, nowMs)) {
                    continue;
                }
                const auto elapsedMs = nowMs - animation.startMs;
                auto canvas = Totem::LedRender::Canvas{finalHsv, renderMap};
                auto ctx = AnimationRenderContext{
                    .clock = FrameClock{.nowMs = nowMs,
                                        .elapsedMs = elapsedMs,
                                        .durationMs = animation.durationMs,
                                        .frame = frame},
                    .hueOffset = request.hueOffset,
                    .canvas = canvas,
                    .inputs = request.inputs,
                };
                Totem::LedRender::Generated::render(animation.payload, ctx);
            }
            if (request.includeScratch) {
                scratchTrace = finalHsv;
            }
        }

        Totem::LedRender::hsvToRgb(finalHsv, finalRgb);
        writeHsvPlane(out, finalHsv, traceMap);
        writeRgbPlane(out, finalRgb, traceMap);
        if (request.includeScratch) {
            writeHsvPlane(out, scratchTrace, traceMap);
        }
    }

    if (!out) {
        error = "Failed while writing output trace " + request.outputPath;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    CliArgs cli{};
    std::string error;
    if (!parseArgs(argc, argv, cli, error)) {
        std::cerr << "led-render: " << error << "\n\n" << usage();
        return 2;
    }
    if (cli.help) {
        std::cout << usage();
        return 0;
    }
    if (cli.list) {
        for (const auto name : Totem::LedRender::Generated::animationNames) {
            std::cout << name << "\n";
        }
        return 0;
    }
    if (cli.configPath.empty()) {
        std::cerr << "led-render: --config is required\n\n" << usage();
        return 2;
    }

    std::string sourceJson;
    if (!readTextFile(cli.configPath, sourceJson, error)) {
        std::cerr << "led-render: " << error << "\n";
        return 1;
    }

    auto parsed = JsonParser{sourceJson}.parse(error);
    if (!parsed) {
        std::cerr << "led-render: " << error << "\n";
        return 1;
    }

    RenderRequest request{};
    request.outputPath = cli.outputPath;
    request.sourceJson = sourceJson;
    request.firstFrame = 0;
    request.lastFrame = 124;

    std::vector<ActiveAnimation> animations;
    if (!loadRequest(*parsed, cli, request, animations, error)) {
        std::cerr << "led-render: " << error << "\n";
        return 1;
    }

    if (!renderTrace(request, cli.configPath, animations, error)) {
        std::cerr << "led-render: " << error << "\n";
        return 1;
    }

    std::cerr << "Wrote " << request.outputPath << " ("
              << Totem::LedRender::frameCount(request.firstFrame,
                                              request.lastFrame)
              << " frames)\n";
    return 0;
}
