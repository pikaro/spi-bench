#pragma once

#include "LoggingBackend/detail/Aggregator.hpp"
#include "LoggingBackend/detail/NativeLogBridge.hpp"
#include "LoggingBackend/detail/Sinks/ConsoleSink.hpp"
#include "LoggingBackend/detail/Sinks/ErrorJournalSink.hpp"

namespace Totem::LoggingBackend {

using detail::Aggregator;
using detail::NativeLogBridge;

using detail::Outputs::ConsoleSink;
using detail::Outputs::ErrorJournalSink;

} // namespace Totem::LoggingBackend
