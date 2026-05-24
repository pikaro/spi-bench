#pragma once

#include <cstdint>

namespace Totem::Audio::detail {
class FftAnalyzer;
class FftDisplay;
} // namespace Totem::Audio::detail

namespace Totem::Buttons::detail {
class Buttons;
} // namespace Totem::Buttons::detail

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
inline constexpr uint32_t command = 6144;
inline constexpr uint32_t ledPwm = 4096;
inline constexpr uint32_t ledDisplay = 8192;
inline constexpr uint32_t buttons = 4096;
inline constexpr uint32_t bluetooth = 4096;
inline constexpr uint32_t pubSubNode = 8192;
inline constexpr uint32_t spiMaster = 8192;
inline constexpr uint32_t spiSlave = 8192;
inline constexpr uint32_t rs485Master = 8192;
inline constexpr uint32_t rs485Slave = 8192;
inline constexpr uint32_t audioFft = 3072;
inline constexpr uint32_t audioFftDisplay = 3072;

} // namespace Totem::StaticConfig::TaskStacks

namespace Totem::TaskController {

template <class Owner> struct StaticStackSize;

template <class Owner> struct StaticTaskStorageEnabled {
    static constexpr bool value =
        Totem::StaticConfig::TaskStacks::defaultTaskStorageStatic;
};

template <>
struct StaticStackSize<Totem::Audio::detail::FftAnalyzer> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::audioFft;
};

template <>
struct StaticStackSize<Totem::Audio::detail::FftDisplay> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::audioFftDisplay;
};

template <>
struct StaticStackSize<Totem::Buttons::detail::Buttons> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::buttons;
};

template <>
struct StaticStackSize<Totem::Bluetooth::detail::Central> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::bluetooth;
};

template <>
struct StaticStackSize<Totem::CommandBackend::detail::Controller> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::command;
};

template <>
struct StaticStackSize<Totem::LedDisplay::detail::Display> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::ledDisplay;
};

template <>
struct StaticStackSize<Totem::LedPwm::detail::LedPwm> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::ledPwm;
};

template <>
struct StaticStackSize<Totem::LoggingBackend::detail::Aggregator> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::loggingAggregator;
};

template <>
struct StaticStackSize<
    Totem::LoggingBackend::detail::Outputs::ErrorJournalSink> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::loggingErrorJournal;
};

template <>
struct StaticStackSize<Totem::PubSubBackend::detail::Node> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::pubSubNode;
};

template <>
struct StaticStackSize<Totem::Wire::Rs485::detail::Master> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::rs485Master;
};

template <>
struct StaticStackSize<Totem::Wire::Rs485::detail::Slave> {
    static constexpr uint32_t value =
        Totem::StaticConfig::TaskStacks::rs485Slave;
};

template <>
struct StaticStackSize<Totem::Wire::Spi::detail::Master> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::spiMaster;
};

template <>
struct StaticStackSize<Totem::Wire::Spi::detail::Slave> {
    static constexpr uint32_t value = Totem::StaticConfig::TaskStacks::spiSlave;
};

} // namespace Totem::TaskController
