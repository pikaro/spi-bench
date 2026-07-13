#pragma once

#include "AudioSink/detail/Sinks/I2SSink.hpp"
#include "AudioSink/detail/Sinks/IAudioSink.hpp"
#include "AudioSink/detail/Sinks/TcpSink.hpp"
#include "AudioSink/detail/Sinks/WebSocketSink.hpp"

namespace Totem::AudioSink {

using detail::I2SSink;
using detail::IAudioSink;
using detail::TcpSink;
using detail::WebSocketSink;

} // namespace Totem::AudioSink
