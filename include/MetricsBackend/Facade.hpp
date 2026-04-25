#pragma once

#include "detail/Backend.hpp"
#include "detail/Recorder.hpp"
#include "detail/Registrar.hpp"

namespace Totem::MetricsBackend {

using Backend = detail::Backend;
using Registrar = detail::Registrar;
using Recorder = detail::Recorder;

} // namespace Totem::MetricsBackend
