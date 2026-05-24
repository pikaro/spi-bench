#pragma once

#include "Base/HasLifecycle.hpp"
#include "Bluetooth/Interfaces/Device.hpp"
#include "Macros/Facade.hpp"
#include "PubSubBackend/Interfaces/Envelope.hpp"
#include "Services/PubSub.hpp"
#include "Types/Angle.hpp"
#include "Types/Error.hpp"
#include "Wheel/Interfaces/Config.hpp"
#include "Wheel/Interfaces/Wire.hpp"
#include "Wheel/detail/Metrics.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Totem::Wheel::detail {

class BleWheel : public HasLifecycle<BleWheel, BleWheelConfig>,
                 public Bluetooth::IDeviceDriver {
    friend class HasLifecycle<BleWheel, BleWheelConfig>;
    friend struct LifecycleContract<BleWheel, BleWheelConfig>;

    using MessagePool = PubSubBackend::Pool<WheelState, 4>;

  public:
    BleWheel() : _messagePool(PubSubService::nextMessageId) {}

    DELETE_COPY(BleWheel)
    DELETE_MOVE(BleWheel)

    static constexpr const char *name = "BleWheel";

    [[nodiscard]] const char *driverName() const override { return name; }

    [[nodiscard]] const char *serviceUuid() const override {
        return config().serviceUuid;
    }

    [[nodiscard]] const char *characteristicUuid() const override {
        return config().angleCharacteristicUuid;
    }

    [[nodiscard]] bool
    matches(const Bluetooth::AdvertisedDevice &device) const override {
        if (config().expectedPeerAddress == nullptr ||
            config().expectedPeerAddress[0] == '\0') {
            return true;
        }

        char address[18]{};
        Bluetooth::formatAddress(device.address, address, sizeof(address));
        return _asciiEqualsIgnoreCase(address, config().expectedPeerAddress);
    }

    void onConnected(const Bluetooth::ConnectionInfo &connection) override {
        char address[18]{};
        Bluetooth::formatAddress(connection.address, address, sizeof(address));
        _log_i("Connected to BLE wheel %s RSSI=%d", address, connection.rssi);
    }

    void onSubscribed(const Bluetooth::SubscriptionInfo &subscription) override {
        _log_i("Subscribed to BLE wheel attr=%u",
               subscription.attributeHandle);
    }

    void onDisconnected(const Bluetooth::ConnectionInfo &connection,
                        int reason) override {
        char address[18]{};
        Bluetooth::formatAddress(connection.address, address, sizeof(address));
        _log_w("Disconnected from BLE wheel %s reason=%d", address, reason);
    }

    void onNotification(const Bluetooth::Notification &notification) override {
        (void)notification.attributeHandle;
        (void)notification.indication;

        if (!active()) {
            metrics().addBad();
            return;
        }
        if (notification.payload.size() != sizeof(float)) {
            _log_w("Bad BLE wheel payload size: %u",
                   static_cast<unsigned>(notification.payload.size()));
            metrics().addBad();
            return;
        }

        float degrees{};
        std::memcpy(&degrees, notification.payload.data(), sizeof(degrees));
        if (!std::isfinite(degrees)) {
            _log_w("Bad BLE wheel angle payload");
            metrics().addBad();
            return;
        }

        auto position = Angle<uint16_t>::fromDeg(degrees);
        auto delta =
            _lastKnown ? position - _lastPosition : Angle<uint16_t>{};
        _lastPosition = position;
        _lastKnown = true;

        metrics().addNotif();
        auto publishRet = _publish(WheelState{
            .position = position,
            .delta = delta,
        });
        if (!publishRet.ok()) {
            metrics().addFail();
            _log_w("Failed to publish BLE wheel state: " ERR_FMT,
                   ERR_ARG(publishRet));
        }
    }

  private:
    ReturnCode _onBegin() {
        (void)metrics();
        FAIL_IF_NOT(PubSubService::configured(), ERR(InvalidState),
                    "PubSub must be configured before %s", name);
        _lastKnown = false;
        _lastPosition = {};
        return OK();
    }

    ReturnCode _onEnd() {
        _lastKnown = false;
        _lastPosition = {};
        return OK();
    }

    ReturnCode _publish(const WheelState &state) {
        auto messageIdResult = _messagePool.store(state);
        if (!messageIdResult) {
            FAIL_ERR_FWD(messageIdResult.error(),
                         "Failed to store wheel state in PubSub pool");
        }
        const auto messageId = *messageIdResult;

        auto envelopeDef = PubSubBackend::EnvelopeDef{
            .owner = static_cast<void *>(&_messagePool),
            .topic = NodeData::PubSub::Topic::Wheel,
            .messageId = messageId,
            .source = static_cast<PubSubBackend::NodeId>(
                PubSubService::get().nodeId()),
            .getPayloadPtr = MessagePool::getPtr,
            .encodePayload = MessagePool::encodePayload,
            .release = MessagePool::release,
            .requireSyncedClock = config().requireSyncedClock,
        };

        auto envelopeResult =
            PubSubBackend::Envelope::make<WheelState>(envelopeDef);
        if (!envelopeResult) {
            _releaseMessage(messageId, "envelope creation");
            FAIL_ERR_FWD(envelopeResult.error(),
                         "Failed to create PubSub envelope for wheel state");
        }

        auto publishResult = PubSubService::get().publish(*envelopeResult);
        if (!publishResult.ok()) {
            _releaseMessage(messageId, "publish failure");
            FAIL_ERR_FWD(publishResult,
                         "Failed to publish wheel state envelope to PubSub");
        }

        metrics().addPublished();
        return OK();
    }

    void _releaseMessage(PubSubBackend::MessageId messageId,
                         const char *context) {
        if (!_messagePool.release({.header = {.messageId = messageId}}).ok()) {
            _log_e("Failed to release wheel state after %s", context);
        }
    }

    static constexpr char _asciiLower(char value) {
        if (value >= 'A' && value <= 'Z') {
            return static_cast<char>(value - 'A' + 'a');
        }
        return value;
    }

    static bool _asciiEqualsIgnoreCase(const char *lhs, const char *rhs) {
        if (lhs == nullptr || rhs == nullptr) {
            return lhs == rhs;
        }
        while (*lhs != '\0' && *rhs != '\0') {
            if (_asciiLower(*lhs) != _asciiLower(*rhs)) {
                return false;
            }
            ++lhs;
            ++rhs;
        }
        return *lhs == '\0' && *rhs == '\0';
    }

    MessagePool _messagePool;
    Angle<uint16_t> _lastPosition{};
    bool _lastKnown = false;

    static constexpr LogComponent logComponent = LogComponent::Input;
};

inline constexpr LifecycleContract<BleWheel, BleWheelConfig>
    _ble_wheel_lifecycle_contract;

} // namespace Totem::Wheel::detail
