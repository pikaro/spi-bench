#pragma once

#include "Output/detail/Aggregator.hh"
#include "Output/detail/Config.hh"
#include "Output/detail/Outputs/Uart.hh"

namespace Totem::Output {

using Aggregator = detail::Aggregator;
using Config = detail::AggregatorConfig;
using Sink = detail::Sink;

using OutputUart = detail::Outputs::OutputUart;

} // namespace Totem::Output
