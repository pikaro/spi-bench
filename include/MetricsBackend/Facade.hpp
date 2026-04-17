#pragma once

#include "MetricsBackend/detail/Types.hh"
#include "detail/Backend.hh"
#include "detail/Recorder.hh"
#include "detail/Registrar.hh"

namespace Totem::MetricsBackend {

using Backend = detail::Backend;
using Registrar = detail::Registrar;
using Recorder = detail::Recorder;
using GroupHandle = detail::GroupHandle;
using CounterHandle = detail::CounterHandle;
using GaugeHandle = detail::GaugeHandle;

} // namespace Totem::MetricsBackend
