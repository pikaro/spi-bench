#include "pubsub_wire.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

using namespace Totem::Tools::PubSubUdp;

constexpr uint16_t defaultPort = 2026;

struct Args {
    std::string mcuIp;
    std::string bindIp = "0.0.0.0";
    uint16_t port = defaultPort;
    uint32_t keepaliveMs = 1000;
    uint32_t timeoutMs = 0;
    bool subscribeButton = true;
    bool help = false;
};

struct Stats {
    uint32_t keepaliveTx = 0;
    uint32_t keepaliveRx = 0;
    uint32_t framesTx = 0;
    uint32_t framesRx = 0;
    uint32_t badFrames = 0;
    uint32_t buttonEvents = 0;
};

volatile std::sig_atomic_t stopping = 0;

void onSignal(int /*signum*/) {
    stopping = 1;
}

std::string usage() {
    return R"(Usage:
  pubsub-udp-peer-cpp --mcu-ip IP [options]

Options:
  --bind-ip IP          Local IPv4 address to bind. Defaults to 0.0.0.0.
  --port PORT           Local and remote UDP port. Defaults to 2026.
  --keepalive-ms MS     Keepalive interval. Defaults to 1000.
  --timeout-ms MS       Stop after this many milliseconds. Defaults to no timeout.
  --no-button-sub       Do not send the initial Button topic subscription.
  --help                Show this help.

Stdin commands:
  publish TOPIC CLASS [PAYLOAD_HEX]
  subscribe TOPIC
  unsubscribe TOPIC
  stats
  quit
)";
}

bool parseUint16(std::string_view text, uint16_t &out) {
    if (text.empty()) {
        return false;
    }
    uint32_t value = 0;
    for (const auto c : text) {
        if (c < '0' || c > '9') {
            return false;
        }
        value = (value * 10U) + static_cast<uint32_t>(c - '0');
        if (value > UINT16_MAX) {
            return false;
        }
    }
    out = static_cast<uint16_t>(value);
    return true;
}

bool parseUint32(std::string_view text, uint32_t &out) {
    if (text.empty()) {
        return false;
    }
    uint64_t value = 0;
    for (const auto c : text) {
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

bool parseUint8(std::string_view text, uint8_t &out) {
    uint32_t value = 0;
    if (!parseUint32(text, value) || value > UINT8_MAX) {
        return false;
    }
    out = static_cast<uint8_t>(value);
    return true;
}

bool parseArgs(int argc, char **argv, Args &args, std::string &error) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        auto requireValue = [&](std::string_view name) -> const char * {
            if (i + 1 >= argc) {
                error = "missing value for " + std::string(name);
                return nullptr;
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            args.help = true;
        } else if (arg == "--mcu-ip") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            args.mcuIp = value;
        } else if (arg == "--bind-ip") {
            const char *value = requireValue(arg);
            if (value == nullptr) {
                return false;
            }
            args.bindIp = value;
        } else if (arg == "--port") {
            const char *value = requireValue(arg);
            if (value == nullptr || !parseUint16(value, args.port) ||
                args.port == 0) {
                error = "invalid --port value";
                return false;
            }
        } else if (arg == "--keepalive-ms") {
            const char *value = requireValue(arg);
            if (value == nullptr || !parseUint32(value, args.keepaliveMs) ||
                args.keepaliveMs == 0) {
                error = "invalid --keepalive-ms value";
                return false;
            }
        } else if (arg == "--timeout-ms") {
            const char *value = requireValue(arg);
            if (value == nullptr || !parseUint32(value, args.timeoutMs)) {
                error = "invalid --timeout-ms value";
                return false;
            }
        } else if (arg == "--no-button-sub") {
            args.subscribeButton = false;
        } else {
            error = "unknown argument: " + std::string(arg);
            return false;
        }
    }
    if (!args.help && args.mcuIp.empty()) {
        error = "--mcu-ip is required";
        return false;
    }
    return true;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8U);
    for (const auto c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

void emitStatus(std::string_view event, std::string_view detail = {}) {
    std::cout << "{\"kind\":\"status\",\"event\":\"" << jsonEscape(event)
              << "\"";
    if (!detail.empty()) {
        std::cout << ",\"detail\":\"" << jsonEscape(detail) << "\"";
    }
    std::cout << "}" << std::endl;
}

void emitStats(const Stats &stats) {
    std::cout << "{\"kind\":\"stats\",\"keepalive_tx\":" << stats.keepaliveTx
              << ",\"keepalive_rx\":" << stats.keepaliveRx
              << ",\"frames_tx\":" << stats.framesTx
              << ",\"frames_rx\":" << stats.framesRx
              << ",\"bad_frames\":" << stats.badFrames
              << ",\"button_events\":" << stats.buttonEvents << "}"
              << std::endl;
}

void emitHeaderJson(const Header &header) {
    std::cout << "\"header\":{"
              << "\"timestamp_ms\":" << header.timestampMs
              << ",\"timestamp_us\":" << header.timestampUs
              << ",\"message_id\":" << header.messageId
              << ",\"topic\":" << header.topic
              << ",\"source\":" << static_cast<unsigned>(header.source)
              << ",\"traffic_class\":"
              << static_cast<unsigned>(header.trafficClass)
              << ",\"payload_size\":" << header.payloadSize << "}";
}

void emitButtonEvent(const Header &header, const ButtonEvent &event) {
    std::cout << "{\"kind\":\"pubsub\","
              << "\"message_type\":\"Totem.Buttons.ButtonEvent\",";
    emitHeaderJson(header);
    std::cout << ",\"payload\":{"
              << "\"type\":\"" << buttonTypeName(event.type) << "\","
              << "\"button\":\"" << buttonName(event.button) << "\""
              << "}}" << std::endl;
}

void emitPubSubEvent(const Header &header, const PubSubEvent &event) {
    std::cout << "{\"kind\":\"pubsub\","
              << "\"message_type\":\"Totem.PubSub.Event\",";
    emitHeaderJson(header);
    std::cout << ",\"payload\":{"
              << "\"topic\":" << event.topic << ","
              << "\"type\":\"" << subscribeTypeName(event.type) << "\""
              << "}}" << std::endl;
}

bool parseIpv4(const std::string &ip, in_addr &out, std::string &error) {
    const auto result = inet_pton(AF_INET, ip.c_str(), &out);
    if (result != 1) {
        error = "invalid IPv4 address: " + ip;
        return false;
    }
    return true;
}

int createSocket(const Args &args, sockaddr_in &remote, std::string &error) {
    in_addr bindAddress{};
    if (!parseIpv4(args.bindIp, bindAddress, error)) {
        return -1;
    }
    in_addr remoteAddress{};
    if (!parseIpv4(args.mcuIp, remoteAddress, error)) {
        return -1;
    }

    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        error = "socket() failed: " + std::string(std::strerror(errno));
        return -1;
    }

    int reuse = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr = bindAddress;
    local.sin_port = htons(args.port);
    if (bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) !=
        0) {
        error = "bind() failed: " + std::string(std::strerror(errno));
        close(fd);
        return -1;
    }

    remote = {};
    remote.sin_family = AF_INET;
    remote.sin_addr = remoteAddress;
    remote.sin_port = htons(args.port);
    return fd;
}

bool sendDatagram(int fd, const sockaddr_in &remote,
                  std::span<const std::byte> payload, std::string &error) {
    const auto sent = sendto(fd, payload.data(), payload.size(), 0,
                             reinterpret_cast<const sockaddr *>(&remote),
                             sizeof(remote));
    if (sent < 0) {
        error = "sendto() failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (static_cast<size_t>(sent) != payload.size()) {
        error = "partial UDP send";
        return false;
    }
    return true;
}

bool sendFrame(int fd, const sockaddr_in &remote,
               std::span<const std::byte> frame, Stats &stats,
               std::string &error) {
    if (!sendDatagram(fd, remote, frame, error)) {
        return false;
    }
    ++stats.framesTx;
    return true;
}

std::optional<uint8_t> parseHexByte(char high, char low) {
    auto nibble = [](char c) -> std::optional<uint8_t> {
        if (c >= '0' && c <= '9') {
            return static_cast<uint8_t>(c - '0');
        }
        if (c >= 'a' && c <= 'f') {
            return static_cast<uint8_t>(10 + c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return static_cast<uint8_t>(10 + c - 'A');
        }
        return std::nullopt;
    };

    const auto highNibble = nibble(high);
    const auto lowNibble = nibble(low);
    if (!highNibble.has_value() || !lowNibble.has_value()) {
        return std::nullopt;
    }
    return static_cast<uint8_t>((*highNibble << 4U) | *lowNibble);
}

std::optional<std::vector<std::byte>>
decodeHexPayload(std::string_view text, std::string &error) {
    if ((text.size() % 2U) != 0) {
        error = "payload hex must have an even number of characters";
        return std::nullopt;
    }
    if ((text.size() / 2U) > maxPayloadSize) {
        error = "payload is too large";
        return std::nullopt;
    }

    std::vector<std::byte> payload;
    payload.reserve(text.size() / 2U);
    for (size_t i = 0; i < text.size(); i += 2U) {
        const auto value = parseHexByte(text[i], text[i + 1U]);
        if (!value.has_value()) {
            error = "payload hex contains a non-hex character";
            return std::nullopt;
        }
        payload.push_back(static_cast<std::byte>(*value));
    }
    return payload;
}

uint64_t nowMs() {
    using Clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
}

bool handleFrame(std::span<const std::byte> datagram, Stats &stats) {
    std::string decodeError;
    auto frame = decodeFrame(datagram, decodeError);
    if (!frame.has_value()) {
        ++stats.badFrames;
        emitStatus("bad-frame", decodeError);
        return true;
    }

    ++stats.framesRx;
    if (frame->header.topic == static_cast<uint32_t>(Topic::Button)) {
        auto button = decodeButtonEvent(frame->payload);
        if (!button.has_value()) {
            ++stats.badFrames;
            emitStatus("bad-button-payload");
            return true;
        }
        ++stats.buttonEvents;
        emitButtonEvent(frame->header, *button);
        return true;
    }
    if (frame->header.topic == static_cast<uint32_t>(Topic::PubSub)) {
        auto event = decodePubSubEvent(frame->payload);
        if (event.has_value()) {
            emitPubSubEvent(frame->header, *event);
        }
        return true;
    }

    std::cout << "{\"kind\":\"pubsub\","
              << "\"message_type\":\"unknown\",";
    emitHeaderJson(frame->header);
    std::cout << "}" << std::endl;
    return true;
}

bool handleCommand(std::string_view line, int fd, const sockaddr_in &remote,
                   uint32_t &nextMessageId, Stats &stats) {
    std::istringstream stream{std::string(line)};
    std::string command;
    stream >> command;
    if (command.empty()) {
        return true;
    }

    std::string error;
    if (command == "help") {
        emitStatus("commands",
                   "publish TOPIC CLASS [PAYLOAD_HEX]; subscribe TOPIC; "
                   "unsubscribe TOPIC; stats; quit");
        return true;
    }

    if (command == "stats") {
        emitStats(stats);
        return true;
    }

    if (command == "quit" || command == "exit" || command == "stop") {
        emitStatus("stopping");
        stopping = 1;
        return true;
    }

    if (command == "publish") {
        std::string topicText;
        std::string trafficClassText;
        std::string payloadHex;
        stream >> topicText >> trafficClassText;
        if (!stream) {
            emitStatus("command-error",
                       "usage: publish <topic> <traffic-class> [payload-hex]");
            return true;
        }
        stream >> payloadHex;

        uint32_t topic = 0;
        uint8_t trafficClass = 0;
        if (!parseUint32(topicText, topic) ||
            !parseUint8(trafficClassText, trafficClass)) {
            emitStatus("command-error", "invalid publish topic or class");
            return true;
        }
        if (trafficClass >
            static_cast<uint8_t>(TrafficClass::Critical)) {
            emitStatus("command-error", "invalid publish traffic class");
            return true;
        }

        auto payload = decodeHexPayload(payloadHex, error);
        if (!payload.has_value()) {
            emitStatus("command-error", error);
            return true;
        }
        auto frame = makeHostFrame(
            nextMessageId++, topic, static_cast<TrafficClass>(trafficClass),
            *payload);
        if (!sendFrame(fd, remote, frame, stats, error)) {
            emitStatus("publish-send-error", error);
        } else {
            emitStatus("published");
        }
        return true;
    }

    if (command == "subscribe" || command == "unsubscribe") {
        std::string topicText;
        stream >> topicText;
        uint32_t topicValue = 0;
        if (!stream || !parseUint32(topicText, topicValue)) {
            emitStatus("command-error",
                       "usage: subscribe <topic> or unsubscribe <topic>");
            return true;
        }
        auto frame = makePubSubControlFrame(
            nextMessageId++, static_cast<Topic>(topicValue),
            command == "subscribe" ? SubscribeEventType::Register
                                   : SubscribeEventType::Unregister);
        if (!sendFrame(fd, remote, frame, stats, error)) {
            emitStatus("subscription-send-error", error);
        } else {
            emitStatus(command == "subscribe" ? "subscribed" : "unsubscribed");
        }
        return true;
    }

    emitStatus("command-error", "unknown command: " + std::string(command));
    return true;
}

void handleInputBuffer(std::string &buffer, int fd,
                       const sockaddr_in &remote, uint32_t &nextMessageId,
                       Stats &stats) {
    for (;;) {
        const auto newline = buffer.find('\n');
        if (newline == std::string::npos) {
            return;
        }
        auto line = buffer.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        buffer.erase(0, newline + 1U);
        (void)handleCommand(line, fd, remote, nextMessageId, stats);
    }
}

int run(const Args &args) {
    std::string error;
    sockaddr_in remote{};
    const int fd = createSocket(args, remote, error);
    if (fd < 0) {
        std::cerr << error << "\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Stats stats{};
    uint32_t nextMessageId = 1;
    uint64_t nextKeepaliveMs = 0;
    const auto startMs = nowMs();
    bool subscriptionSent = false;
    bool stdinOpen = true;
    std::string inputBuffer;
    emitStatus("started");

    while (stopping == 0) {
        const auto currentMs = nowMs();
        if (args.timeoutMs != 0 &&
            currentMs - startMs >= args.timeoutMs) {
            emitStatus("timeout");
            break;
        }

        if (currentMs >= nextKeepaliveMs) {
            if (!sendDatagram(fd, remote, keepalivePacket, error)) {
                emitStatus("send-error", error);
            } else {
                ++stats.keepaliveTx;
            }
            nextKeepaliveMs = currentMs + args.keepaliveMs;
        }

        if (args.subscribeButton && !subscriptionSent) {
            auto frame = makeSubscribeFrame(nextMessageId++, Topic::Button);
            if (!sendDatagram(fd, remote, frame, error)) {
                emitStatus("subscription-send-error", error);
            } else {
                ++stats.framesTx;
                emitStatus("subscribed-button");
                subscriptionSent = true;
            }
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        int maxFd = fd;
        if (stdinOpen) {
            FD_SET(STDIN_FILENO, &readfds);
            maxFd = std::max(maxFd, STDIN_FILENO);
        }
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        const auto ready =
            select(maxFd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            emitStatus("select-error", std::strerror(errno));
            break;
        }
        if (stdinOpen && FD_ISSET(STDIN_FILENO, &readfds)) {
            char input[512]{};
            const auto readCount =
                read(STDIN_FILENO, input, sizeof(input));
            if (readCount <= 0) {
                stdinOpen = false;
            } else {
                inputBuffer.append(input, static_cast<size_t>(readCount));
                handleInputBuffer(inputBuffer, fd, remote, nextMessageId,
                                  stats);
            }
        }
        if (ready == 0 || !FD_ISSET(fd, &readfds)) {
            continue;
        }

        std::array<std::byte, 2300> buffer{};
        sockaddr_in source{};
        socklen_t sourceLen = sizeof(source);
        const auto received =
            recvfrom(fd, buffer.data(), buffer.size(), 0,
                     reinterpret_cast<sockaddr *>(&source), &sourceLen);
        if (received < 0) {
            emitStatus("receive-error", std::strerror(errno));
            continue;
        }

        auto datagram = std::span<const std::byte>{
            buffer.data(), static_cast<size_t>(received)};
        if (isKeepalive(datagram)) {
            ++stats.keepaliveRx;
            continue;
        }
        (void)handleFrame(datagram, stats);
    }

    emitStats(stats);
    close(fd);
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    Args args{};
    std::string error;
    if (!parseArgs(argc, argv, args, error)) {
        std::cerr << error << "\n\n" << usage();
        return 2;
    }
    if (args.help) {
        std::cout << usage();
        return 0;
    }
    return run(args);
}
