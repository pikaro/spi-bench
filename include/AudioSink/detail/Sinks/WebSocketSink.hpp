#pragma once

#include "AudioSink/Interfaces/SinkConfig.hpp"
#include "AudioSink/Interfaces/Types.hpp"
#include "AudioSink/detail/PlatformSelect.hpp"
#include "AudioSink/detail/Sinks/IAudioSink.hpp"
#include "AudioSink/detail/Types.hpp"
#include "Base/HasLifecycle.hpp"
#include "Macros/Facade.hpp"
#include "Types/Error.hpp"

namespace Totem::AudioSink::detail {

class WebSocketSink
    : public HasLifecycle<WebSocketSink, WebSocketSinkConfig>,
      public IAudioSink {
    friend class HasLifecycle<WebSocketSink, WebSocketSinkConfig>;
    friend struct LifecycleContract<WebSocketSink, WebSocketSinkConfig>;

  public:
    DELETE_COPY(WebSocketSink)
    DELETE_MOVE(WebSocketSink)

    static constexpr const char *name = "AudioSink::WebSocketSink";
    static constexpr LogComponent logComponent =
        Totem::AudioSink::detail::logComponent;

    WebSocketSink() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<WebSocketSink, WebSocketSinkConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _stream.audioInfo();
    }
    [[nodiscard]] bool ready() const override { return _stream.ready(); }
    [[nodiscard]] const char *sinkName() const override { return "websocket"; }
    [[nodiscard]] AudioSinkStatus status() const { return _stream.status(); }

    Platform::AudioStream &stream() override { return _stream; }

  private:
    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(_stream.begin(this->config()),
                        "Failed to start WebSocket audio sink");
        return OK();
    }

    ReturnCode _onEnd() { return _stream.close(); }

    Platform::WebSocketOutputStream _stream{};
};

} // namespace Totem::AudioSink::detail
