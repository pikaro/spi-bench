#pragma once

namespace Totem::Wheel {

struct BleWheelConfig {
    const char *serviceUuid = "DE516A65-57B9-471C-ACC3-4F295834594C";
    const char *angleCharacteristicUuid =
        "B9E414E0-388D-4A46-9146-01D4449FC2A2";
    const char *expectedPeerAddress = nullptr;
    bool requireSyncedClock = false;

    [[nodiscard]] constexpr bool validate() const {
        return serviceUuid != nullptr && serviceUuid[0] != '\0' &&
               angleCharacteristicUuid != nullptr &&
               angleCharacteristicUuid[0] != '\0';
    }
};

} // namespace Totem::Wheel
