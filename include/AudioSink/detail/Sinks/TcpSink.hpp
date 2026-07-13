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

class TcpSink : public HasLifecycle<TcpSink, TcpSinkConfig>,
                public IAudioSink {
    friend class HasLifecycle<TcpSink, TcpSinkConfig>;
    friend struct LifecycleContract<TcpSink, TcpSinkConfig>;

  public:
    DELETE_COPY(TcpSink)
    DELETE_MOVE(TcpSink)

    static constexpr const char *name = "AudioSink::TcpSink";
    static constexpr LogComponent logComponent =
        Totem::AudioSink::detail::logComponent;

    TcpSink() = default;

    [[nodiscard]] bool active() const override {
        return HasLifecycle<TcpSink, TcpSinkConfig>::active();
    }
    [[nodiscard]] const AudioInfo &audioInfo() const override {
        return _stream.audioInfo();
    }
    [[nodiscard]] bool ready() const override { return _stream.ready(); }
    [[nodiscard]] const char *sinkName() const override { return "tcp"; }
    [[nodiscard]] AudioSinkStatus status() const { return _stream.status(); }

    Platform::AudioStream &stream() override { return _stream; }

  private:
    ReturnCode _onBegin() {
        FAIL_IF_ERR_FWD(_stream.begin(this->config().network),
                        "Failed to start TCP audio sink");
        return OK();
    }

    ReturnCode _onEnd() { return _stream.close(); }

    Platform::TcpOutputStream _stream{};
};

} // namespace Totem::AudioSink::detail
