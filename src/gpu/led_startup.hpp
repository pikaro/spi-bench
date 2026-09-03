#pragma once

#include "Data/PubSub.hpp"
#include "LedDisplay/Interfaces/OutputStartup.hpp"
#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/Gpio.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Services/PubSub.hpp"
#include "Types/Error.hpp"
#include <cstdint>

class LedStartup {
  public:
    ReturnCode holdOutputGateDisabled(Pin pin) {
        FAIL_IF(_gateConfigured, ERR(CoreError, InvalidState),
                "LED output gate is already configured");
        FAIL_IF_ERR_FWD(
            _gate.initOutput(pin, GpioOutputMode::PushPull, gateDisabledLevel),
            "Failed to hold LED output gate inactive");
        _gateConfigured = true;
        return OK();
    }

    ReturnCode begin(bool ownsOutputGate) {
        FAIL_IF(_begun, ERR(CoreError, InvalidState),
                "GPU LED startup is already configured");
        FAIL_IF(ownsOutputGate && !_gateConfigured,
                ERR(CoreError, InvalidState),
                "GPU owns an unconfigured LED output gate");
        FAIL_IF_NOT(PubSubService::configured(), ERR(CoreError, InvalidState),
                    "PubSub backend is not configured for GPU LED startup");

        _ownsOutputGate = ownsOutputGate;
        if (_ownsOutputGate) {
            FAIL_IF_UNEXPECTED_FWD(
                subscription,
                PubSubService::get().subscribe(
                    "led-output-en",
                    {.subscriber = this, .callback = _onEnableEnvelope},
                    PubSubService::Topic::LedOutputEnable),
                "Failed to subscribe to LED output-enable commands");
            _enableSubscription = subscription;
        }
        _begun = true;
        if (_ownsOutputGate) {
            _log_i("Local LED output is ready; awaiting master enable command");
        } else {
            _log_i("Local LED output is ready");
        }
        return OK();
    }

    ReturnCode work(uint32_t nowMs) {
        FAIL_IF(!_begun, ERR(CoreError, InvalidState),
                "GPU LED startup is not configured");
        if (_hasPublishedReady &&
            static_cast<uint32_t>(nowMs - _lastReadyPublishMs) <
                readyPublishIntervalMs) {
            return OK();
        }

        _hasPublishedReady = true;
        _lastReadyPublishMs = nowMs;
        return PubSubService::publish(PubSubService::Topic::LedOutputReady,
                                      Totem::LedDisplay::OutputReadyEvent{});
    }

  private:
    static constexpr bool gateDisabledLevel = true;
    static constexpr bool gateEnabledLevel = false;
    static constexpr uint32_t readyPublishIntervalMs = 1000;

    ReturnCode _setEnabled(bool enabled) {
        if (_ownsOutputGate) {
            FAIL_IF_ERR_FWD(
                _gate.setLevel(enabled ? gateEnabledLevel : gateDisabledLevel),
                "Failed to change LED output gate");
        }

        if (_enableReceived != enabled) {
            _enableReceived = enabled;
            if (enabled) {
                _log_i("Master enabled LED output");
            } else {
                _log_i("Master disabled LED output");
            }
        }
        return OK();
    }

    static ReturnCode
    _onEnableEnvelope(void *owner,
                      const Totem::PubSubBackend::Envelope &envelope) {
        auto *self = static_cast<LedStartup *>(owner);
        FAIL_IF_NULL(self, ERR(CoreError, InvalidArgument),
                     "GPU LED startup subscriber owner is null");
        FAIL_IF(envelope.header.source !=
                    static_cast<uint16_t>(Totem::Data::PubSub::NodeId::Master),
                ERR(CoreError, InvalidArgument),
                "LED output-enable command did not come from master");
        FAIL_IF_UNEXPECTED_FWD(
            command,
            envelope.getPayloadAs<Totem::LedDisplay::OutputEnableCommand>(),
            "Failed to decode LED output-enable command");
        return self->_setEnabled(command.enabled);
    }

    platform::Gpio _gate;
    Totem::PubSubBackend::SubscriberKey _enableSubscription = 0;
    uint32_t _lastReadyPublishMs = 0;
    bool _gateConfigured = false;
    bool _ownsOutputGate = false;
    bool _begun = false;
    bool _hasPublishedReady = false;
    bool _enableReceived = false;

    static constexpr LogComponent logComponent = LogComponent::Output;
};
