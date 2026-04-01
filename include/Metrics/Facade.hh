#pragma once

#include "Metrics/detail/Types.hh"
#include "detail/Backend.hh"
#include "detail/Recorder.hh"
#include "detail/Registrar.hh"

namespace Totem::Metrics {

using Backend = detail::Backend;
using Registrar = detail::Registrar;
using Recorder = detail::Recorder;
using GroupHandle = detail::GroupHandle;
using CounterHandle = detail::CounterHandle;
using GaugeHandle = detail::GaugeHandle;

} // namespace Totem::Metrics
