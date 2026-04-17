#pragma once

#include "MetricsBackend/detail/Types.hpp"
#include "detail/Backend.hpp"
#include "detail/Recorder.hpp"
#include "detail/Registrar.hpp"

namespace Totem::MetricsBackend {

using Backend = detail::Backend;
using Registrar = detail::Registrar;
using Recorder = detail::Recorder;
using GroupHandle = detail::GroupHandle;
using CounterHandle = detail::CounterHandle;
using GaugeHandle = detail::GaugeHandle;

} // namespace Totem::MetricsBackend
