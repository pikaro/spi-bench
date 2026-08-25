#pragma once

#include <cstdint>

namespace Totem::AudioFft::detail {
class FftAnalyzer;
class FftDisplay;
} // namespace Totem::AudioFft::detail

namespace Totem::AudioAfe::detail {
class AfeProcessor;
} // namespace Totem::AudioAfe::detail

namespace Totem::PubSubEventProducer::detail {
class Producer;
} // namespace Totem::PubSubEventProducer::detail

namespace Totem::Bluetooth::detail {
class Central;
} // namespace Totem::Bluetooth::detail

namespace Totem::CommandBackend::detail {
class Controller;
} // namespace Totem::CommandBackend::detail

namespace Totem::LedDisplay::detail {
class Display;
} // namespace Totem::LedDisplay::detail

namespace Totem::LedPwm::detail {
class LedPwm;
} // namespace Totem::LedPwm::detail

namespace Totem::LoggingBackend::detail {
class Aggregator;
namespace Outputs {
class ErrorJournalSink;
} // namespace Outputs
} // namespace Totem::LoggingBackend::detail

namespace Totem::PubSubBackend::detail {
class Node;
} // namespace Totem::PubSubBackend::detail

namespace Totem::PubSubBackend::Transports {
class UdpTransport;
} // namespace Totem::PubSubBackend::Transports

namespace Totem::Wire::Rs485::detail {
class Master;
class Slave;
} // namespace Totem::Wire::Rs485::detail

namespace Totem::Wire::Spi::detail {
class Master;
class Slave;
} // namespace Totem::Wire::Spi::detail

namespace Totem::StaticConfig::TaskStacks {

inline constexpr bool defaultTaskStorageStatic = true;

inline constexpr uint32_t loggingAggregator = 4096;
inline constexpr uint32_t loggingErrorJournal = 4096;
inline constexpr uint32_t command = 10240;
inline constexpr uint32_t ledPwm = 4096;
inline constexpr uint32_t ledDisplay = 8192;
inline constexpr uint32_t pubSubEventProducer = 4096;
inline constexpr uint32_t bluetooth = 4096;
inline constexpr uint32_t pubSubNode = 8192;
inline constexpr uint32_t spiMaster = 8192;
inline constexpr uint32_t spiSlave = 8192;
inline constexpr uint32_t rs485Master = 8192;
inline constexpr uint32_t rs485Slave = 8192;
inline constexpr uint32_t audioFft = 3072;
inline constexpr uint32_t audioFftDisplay = 3072;
inline constexpr uint32_t audioAfe = 8192;
inline constexpr uint32_t pubSubUdp = 4096;

} // namespace Totem::StaticConfig::TaskStacks

namespace Totem::TaskController {

template <class Owner> struct StaticStackSize;

template <class Owner> struct StaticTaskStorageEnabled {
    static constexpr bool value =
        Totem::StaticConfig::TaskStacks::defaultTaskStorageStatic;
};

template <> struct StaticStackSize<Totem::AudioFft::detail::FftAnalyzer> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::audioFft;
};

template <> struct StaticStackSize<Totem::AudioFft::detail::FftDisplay> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::audioFftDisplay;
};

template <> struct StaticStackSize<Totem::AudioAfe::detail::AfeProcessor> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::audioAfe;
};

template <>
struct StaticStackSize<Totem::PubSubEventProducer::detail::Producer> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::pubSubEventProducer;
};

template <> struct StaticStackSize<Totem::Bluetooth::detail::Central> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::bluetooth;
};

template <> struct StaticStackSize<Totem::CommandBackend::detail::Controller> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::command;
};

template <> struct StaticStackSize<Totem::LedDisplay::detail::Display> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::ledDisplay;
};

template <> struct StaticStackSize<Totem::LedPwm::detail::LedPwm> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::ledPwm;
};

template <> struct StaticStackSize<Totem::LoggingBackend::detail::Aggregator> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::loggingAggregator;
};

template <>
struct StaticStackSize<
    Totem::LoggingBackend::detail::Outputs::ErrorJournalSink> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::loggingErrorJournal;
};

template <> struct StaticStackSize<Totem::PubSubBackend::detail::Node> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::pubSubNode;
};

template <>
struct StaticStackSize<Totem::PubSubBackend::Transports::UdpTransport> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::pubSubUdp;
};

template <> struct StaticStackSize<Totem::Wire::Rs485::detail::Master> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::rs485Master;
};

template <> struct StaticStackSize<Totem::Wire::Rs485::detail::Slave> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::rs485Slave;
};

template <> struct StaticStackSize<Totem::Wire::Spi::detail::Master> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::spiMaster;
};

template <> struct StaticStackSize<Totem::Wire::Spi::detail::Slave> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::spiSlave;
};

} // namespace Totem::TaskController
