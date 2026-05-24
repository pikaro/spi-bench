// IWYU pragma: private

#pragma once

#include "Bluetooth/Interfaces/Config.hpp"
#include "Bluetooth/Interfaces/Types.hpp"
#include "Bluetooth/detail/Metrics.hpp"
#include "Bluetooth/detail/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "StaticConfig/Bluetooth.hpp"
#include "Types/Error.hpp"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" void ble_store_config_init(void);

namespace Totem::Bluetooth::detail::platform {

class NimbleCentral {
  public:
    DELETE_COPY(NimbleCentral)
    DELETE_MOVE(NimbleCentral)

    NimbleCentral() = default;
    ~NimbleCentral() { (void)end(); }

    ReturnCode begin(const Config &config, NotificationSinkBinding sink) {
        FAIL_IF(_active != nullptr, ERR(LifecycleError, Active),
                "Only one BLE central can be active");
        FAIL_IF_NOT(sink.valid(), ERR(InvalidArgument),
                    "Bluetooth notification sink is invalid");

        _config = config;
        _sink = sink;
        _stopping = false;
        _clearConnection(false, 0);
        _setState(State::Idle);
        FAIL_IF_ERR_FWD(_prepareDrivers(), "Failed to prepare BLE drivers");
        FAIL_IF_ERR_FWD(_initNvs(), "Failed to initialize NVS for BLE");

        const auto initRet = nimble_port_init();
        FAIL_IF(initRet != ESP_OK, ERR(OperationFailed),
                "Failed to initialize NimBLE: rc=%d", initRet);

        _active = this;
        _initialized = true;
        ble_hs_cfg.reset_cb = _onReset;
        ble_hs_cfg.sync_cb = _onSync;
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_store_config_init();

        nimble_port_freertos_init(_hostTask);
        _hostStarted = true;
        _log_i("NimBLE central starting with %u driver(s)",
               static_cast<unsigned>(_config.driverCount));
        return OK();
    }

    ReturnCode end() {
        _stopping = true;
        auto ret = OK();
        if (_connected()) {
            const auto termRet =
                ble_gap_terminate(_connection.connHandle,
                                  BLE_ERR_REM_USER_CONN_TERM);
            if (termRet != 0) {
                _log_w("BLE terminate during end returned rc=%d", termRet);
            }
        }
        if (_hostStarted) {
            const auto stopRet = nimble_port_stop();
            if (stopRet != 0) {
                _log_w("NimBLE stop returned rc=%d", stopRet);
                ret.combine(ERR(OperationFailed));
            }
            _hostStarted = false;
        }
        if (_initialized) {
            nimble_port_deinit();
            _initialized = false;
        }
        if (_active == this) {
            _active = nullptr;
        }
        _clearConnection(false, 0);
        _setState(State::Idle);
        return ret;
    }

  private:
    enum class State : uint8_t {
        Idle,
        Scanning,
        Connecting,
        DiscoveringService,
        DiscoveringCharacteristic,
        DiscoveringDescriptor,
        Subscribing,
        Subscribed,
        Disconnecting,
    };

    struct DriverSlot {
        IDeviceDriver *driver{};
        ble_uuid_any_t serviceUuid{};
        ble_uuid_any_t characteristicUuid{};
    };

    static void _hostTask(void * /*param*/) {
        _log_i("NimBLE host task started");
        nimble_port_run();
        nimble_port_freertos_deinit();
    }

    static void _onReset(int reason) {
        if (_active == nullptr) {
            return;
        }
        _log_w("NimBLE host reset: reason=%d", reason);
        _active->_clearConnection(true, reason);
        _active->_setState(State::Idle);
        metrics().addFail();
    }

    static void _onSync() {
        if (_active == nullptr) {
            return;
        }
        const auto rc = ble_hs_util_ensure_addr(0);
        if (rc != 0) {
            _log_e("Failed to ensure BLE address: rc=%d", rc);
            metrics().addFail();
            return;
        }
        _active->_startScan();
    }

    static int _gapEvent(struct ble_gap_event *event, void *arg) {
        auto *self = static_cast<NimbleCentral *>(arg);
        if (self == nullptr) {
            self = _active;
        }
        if (self == nullptr || event == nullptr) {
            return 0;
        }
        return self->_handleGapEvent(*event);
    }

    static int _serviceDiscovered(uint16_t connHandle,
                                  const ble_gatt_error *error,
                                  const ble_gatt_svc *service, void *arg) {
        auto *self = static_cast<NimbleCentral *>(arg);
        return self == nullptr
                   ? 0
                   : self->_handleService(connHandle, error, service);
    }

    static int _characteristicDiscovered(uint16_t connHandle,
                                         const ble_gatt_error *error,
                                         const ble_gatt_chr *characteristic,
                                         void *arg) {
        auto *self = static_cast<NimbleCentral *>(arg);
        return self == nullptr ? 0
                               : self->_handleCharacteristic(
                                     connHandle, error, characteristic);
    }

    static int _descriptorDiscovered(uint16_t connHandle,
                                     const ble_gatt_error *error,
                                     uint16_t chrValueHandle,
                                     const ble_gatt_dsc *descriptor,
                                     void *arg) {
        auto *self = static_cast<NimbleCentral *>(arg);
        return self == nullptr
                   ? 0
                   : self->_handleDescriptor(connHandle, error, chrValueHandle,
                                             descriptor);
    }

    static int _subscribed(uint16_t connHandle, const ble_gatt_error *error,
                           ble_gatt_attr *attribute, void *arg) {
        auto *self = static_cast<NimbleCentral *>(arg);
        return self == nullptr
                   ? 0
                   : self->_handleSubscribed(connHandle, error, attribute);
    }

    ReturnCode _prepareDrivers() {
        for (auto &slot : _drivers) {
            slot = {};
        }

        for (size_t i = 0; i < _config.driverCount; ++i) {
            auto *driver = _config.drivers[i];
            FAIL_IF_NULL(driver, ERR(InvalidArgument),
                         "BLE driver slot %u is null", static_cast<unsigned>(i));
            auto &slot = _drivers[i];
            slot.driver = driver;
            auto serviceRet =
                ble_uuid_from_str(&slot.serviceUuid, driver->serviceUuid());
            FAIL_IF(serviceRet != 0, ERR(InvalidArgument),
                    "Invalid BLE service UUID for driver %s: %s rc=%d",
                    driver->driverName(), driver->serviceUuid(), serviceRet);
            auto charRet = ble_uuid_from_str(&slot.characteristicUuid,
                                             driver->characteristicUuid());
            FAIL_IF(charRet != 0, ERR(InvalidArgument),
                    "Invalid BLE characteristic UUID for driver %s: %s rc=%d",
                    driver->driverName(), driver->characteristicUuid(),
                    charRet);
        }
        return OK();
    }

    static ReturnCode _initNvs() {
        auto ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            FAIL_IF_PLATFORM_FWD(nvs_flash_erase(),
                                 "Failed to erase stale NVS before BLE init");
            ret = nvs_flash_init();
        }
        FAIL_IF_PLATFORM_FWD(ret, "Failed to initialize NVS before BLE init");
        return OK();
    }

    void _setState(State state) {
        _state = state;
        metrics().setState(static_cast<uint32_t>(state));
    }

    [[nodiscard]] bool _connected() const {
        return _activeDriver != nullptr;
    }

    static bool _isConnectable(uint8_t eventType) {
        return eventType == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
               eventType == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
    }

    static Address _snapshotAddress(const ble_addr_t &addr) {
        Address out{.type = addr.type};
        std::memcpy(out.value.data(), addr.val, out.value.size());
        return out;
    }

    static void _formatAddress(const ble_addr_t &addr, char *out,
                               size_t outSize) {
        formatAddress(_snapshotAddress(addr), out, outSize);
    }

    static bool _advertisesUuid(const ble_hs_adv_fields &fields,
                                const ble_uuid_t &uuid) {
        for (uint8_t i = 0; i < fields.num_uuids16; ++i) {
            if (ble_uuid_cmp(&fields.uuids16[i].u, &uuid) == 0) {
                return true;
            }
        }
        for (uint8_t i = 0; i < fields.num_uuids32; ++i) {
            if (ble_uuid_cmp(&fields.uuids32[i].u, &uuid) == 0) {
                return true;
            }
        }
        for (uint8_t i = 0; i < fields.num_uuids128; ++i) {
            if (ble_uuid_cmp(&fields.uuids128[i].u, &uuid) == 0) {
                return true;
            }
        }
        return false;
    }

    DriverSlot *_matchDriver(const ble_gap_disc_desc &disc,
                             const ble_hs_adv_fields &fields) {
        const auto connectable = _isConnectable(disc.event_type);
        if (!connectable) {
            return nullptr;
        }

        auto view = AdvertisedDevice{
            .address = _snapshotAddress(disc.addr),
            .rssi = disc.rssi,
            .connectable = connectable,
        };

        for (size_t i = 0; i < _config.driverCount; ++i) {
            auto &slot = _drivers[i];
            if (slot.driver == nullptr ||
                !_advertisesUuid(fields, slot.serviceUuid.u)) {
                continue;
            }
            if (slot.driver->matches(view)) {
                return &slot;
            }
        }
        return nullptr;
    }

    int _handleGapEvent(const ble_gap_event &event) {
        switch (event.type) {
        case BLE_GAP_EVENT_DISC:
            return _handleDiscovery(event.disc);
        case BLE_GAP_EVENT_DISC_COMPLETE:
            _log_i("BLE discovery complete: reason=%d",
                   event.disc_complete.reason);
            if (_state == State::Scanning && !_stopping) {
                _setState(State::Idle);
                _startScan();
            }
            return 0;
        case BLE_GAP_EVENT_CONNECT:
            return _handleConnect(event);
        case BLE_GAP_EVENT_DISCONNECT:
            _log_w("BLE disconnected: reason=%d",
                   event.disconnect.reason);
            metrics().addDisc();
            _clearConnection(true, event.disconnect.reason);
            if (!_stopping) {
                _startScan();
            }
            return 0;
        case BLE_GAP_EVENT_NOTIFY_RX:
            return _handleNotify(event);
        case BLE_GAP_EVENT_MTU:
            _log_i("BLE MTU update: conn=%u mtu=%u",
                   event.mtu.conn_handle, event.mtu.value);
            return 0;
        case BLE_GAP_EVENT_SUBSCRIBE:
            _log_i("BLE subscribe event: conn=%u attr=%u notify=%u indicate=%u",
                   event.subscribe.conn_handle, event.subscribe.attr_handle,
                   event.subscribe.cur_notify, event.subscribe.cur_indicate);
            return 0;
        default:
            return 0;
        }
    }

    int _handleDiscovery(const ble_gap_disc_desc &disc) {
        if (_state != State::Scanning) {
            return 0;
        }

        ble_hs_adv_fields fields{};
        const auto parseRet =
            ble_hs_adv_parse_fields(&fields, disc.data, disc.length_data);
        if (parseRet != 0) {
            return 0;
        }

        auto *slot = _matchDriver(disc, fields);
        if (slot == nullptr) {
            return 0;
        }

        char addr[18]{};
        _formatAddress(disc.addr, addr, sizeof(addr));
        _log_i("BLE target %s matched driver %s RSSI=%d", addr,
               slot->driver->driverName(), disc.rssi);
        _pendingDriver = slot->driver;
        _pendingDriverSlot = slot;
        _pendingAddress = _snapshotAddress(disc.addr);
        _pendingRssi = disc.rssi;
        _setState(State::Connecting);

        const auto cancelRet = ble_gap_disc_cancel();
        if (cancelRet != 0) {
            _log_w("BLE scan cancel before connect returned rc=%d", cancelRet);
        }

        uint8_t ownAddrType{};
        const auto addrRet = ble_hs_id_infer_auto(0, &ownAddrType);
        if (addrRet != 0) {
            _log_e("BLE own address infer failed: rc=%d", addrRet);
            metrics().addFail();
            _clearConnection(false, addrRet);
            _startScan();
            return 0;
        }

        const auto connectRet =
            ble_gap_connect(ownAddrType, &disc.addr, _config.connectTimeoutMs,
                            nullptr, _gapEvent, this);
        if (connectRet != 0) {
            _log_e("BLE connect failed to start: rc=%d", connectRet);
            metrics().addFail();
            _clearConnection(false, connectRet);
            _startScan();
        }
        return 0;
    }

    int _handleConnect(const ble_gap_event &event) {
        if (event.connect.status != 0) {
            _log_w("BLE connection failed: status=%d", event.connect.status);
            metrics().addFail();
            _clearConnection(false, event.connect.status);
            if (!_stopping) {
                _startScan();
            }
            return 0;
        }

        if (_pendingDriver == nullptr || _pendingDriverSlot == nullptr) {
            _log_e("BLE connected without a pending driver");
            metrics().addFail();
            (void)ble_gap_terminate(event.connect.conn_handle,
                                    BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }

        _activeDriver = _pendingDriver;
        _activeDriverSlot = _pendingDriverSlot;
        _connection = {
            .address = _pendingAddress,
            .connHandle = event.connect.conn_handle,
            .rssi = _pendingRssi,
        };
        _pendingDriver = nullptr;
        _pendingDriverSlot = nullptr;
        metrics().addConn();
        _activeDriver->onConnected(_connection);
        _discoverService();
        return 0;
    }

    void _discoverService() {
        _serviceFound = false;
        _setState(State::DiscoveringService);
        const auto ret = ble_gattc_disc_svc_by_uuid(
            _connection.connHandle, &_activeDriverSlot->serviceUuid.u,
            _serviceDiscovered, this);
        if (ret != 0) {
            _log_e("BLE service discovery failed to start: rc=%d", ret);
            metrics().addFail();
            _terminateConnection(ret);
        }
    }

    void _discoverCharacteristic(uint16_t connHandle) {
        _characteristicFound = false;
        _setState(State::DiscoveringCharacteristic);
        const auto ret = ble_gattc_disc_chrs_by_uuid(
            connHandle, _serviceStartHandle, _serviceEndHandle,
            &_activeDriverSlot->characteristicUuid.u,
            _characteristicDiscovered, this);
        if (ret != 0) {
            _log_e("BLE characteristic discovery failed to start: rc=%d", ret);
            metrics().addFail();
            _terminateConnection(ret);
        }
    }

    void _discoverDescriptor(uint16_t connHandle) {
        _cccdHandle = 0;
        _setState(State::DiscoveringDescriptor);
        const auto ret = ble_gattc_disc_all_dscs(
            connHandle, _characteristicHandle, _serviceEndHandle,
            _descriptorDiscovered, this);
        if (ret != 0) {
            _log_e("BLE descriptor discovery failed to start: rc=%d", ret);
            metrics().addFail();
            _terminateConnection(ret);
        }
    }

    void _subscribe(uint16_t connHandle) {
        _setState(State::Subscribing);
        const uint8_t cccdValue[2] = {0x01, 0x00};
        const auto ret =
            ble_gattc_write_flat(connHandle, _cccdHandle, cccdValue,
                                 sizeof(cccdValue), _subscribed, this);
        if (ret != 0) {
            _log_e("BLE CCCD write failed to start: rc=%d", ret);
            metrics().addFail();
            _terminateConnection(ret);
        }
    }

    int _handleService(uint16_t connHandle, const ble_gatt_error *error,
                       const ble_gatt_svc *service) {
        if (!_sameConnection(connHandle)) {
            return 0;
        }
        if (error == nullptr) {
            _log_e("BLE service discovery failed without status");
            metrics().addFail();
            _terminateConnection(-1);
            return 0;
        }
        if (error->status == 0) {
            if (service == nullptr) {
                _log_e("BLE service discovery returned null service");
                metrics().addFail();
                _terminateConnection(-1);
                return 0;
            }

            _serviceStartHandle = service->start_handle;
            _serviceEndHandle = service->end_handle;
            _serviceFound = true;
            return 0;
        }
        if (error->status == BLE_HS_EDONE) {
            if (!_serviceFound) {
                _log_e("BLE service not found");
                metrics().addFail();
                _terminateConnection(error->status);
                return 0;
            }
            _discoverCharacteristic(connHandle);
            return 0;
        }
        {
            const auto status = error == nullptr ? -1 : error->status;
            _log_e("BLE service discovery failed: status=%d", status);
            metrics().addFail();
            _terminateConnection(status);
            return 0;
        }
    }

    int _handleCharacteristic(uint16_t connHandle,
                              const ble_gatt_error *error,
                              const ble_gatt_chr *characteristic) {
        if (!_sameConnection(connHandle)) {
            return 0;
        }
        if (error == nullptr) {
            _log_e("BLE characteristic discovery failed without status");
            metrics().addFail();
            _terminateConnection(-1);
            return 0;
        }
        if (error->status == 0) {
            if (characteristic == nullptr) {
                _log_e("BLE characteristic discovery returned null char");
                metrics().addFail();
                _terminateConnection(-1);
                return 0;
            }
            if ((characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY) == 0) {
                _log_e("BLE characteristic is not notifiable");
                metrics().addFail();
                _terminateConnection(BLE_HS_ENOTSUP);
                return 0;
            }

            _characteristicHandle = characteristic->val_handle;
            _characteristicFound = true;
            return 0;
        }
        if (error->status == BLE_HS_EDONE) {
            if (!_characteristicFound) {
                _log_e("BLE characteristic not found");
                metrics().addFail();
                _terminateConnection(error->status);
                return 0;
            }
            _discoverDescriptor(connHandle);
            return 0;
        }
        {
            const auto status = error->status;
            _log_e("BLE characteristic discovery failed: status=%d", status);
            metrics().addFail();
            _terminateConnection(status);
            return 0;
        }
    }

    int _handleDescriptor(uint16_t connHandle, const ble_gatt_error *error,
                          uint16_t chrValueHandle,
                          const ble_gatt_dsc *descriptor) {
        if (!_sameConnection(connHandle) ||
            chrValueHandle != _characteristicHandle) {
            return 0;
        }

        static const ble_uuid16_t cccdUuid =
            BLE_UUID16_INIT(BLE_GATT_DSC_CLT_CFG_UUID16);

        if (error == nullptr) {
            _log_e("BLE descriptor discovery failed without status");
            metrics().addFail();
            _terminateConnection(-1);
            return 0;
        }

        if (error->status == 0 && descriptor != nullptr) {
            if (ble_uuid_cmp(&descriptor->uuid.u, &cccdUuid.u) != 0) {
                return 0;
            }

            _cccdHandle = descriptor->handle;
            return 0;
        }

        if (error->status == BLE_HS_EDONE) {
            if (_cccdHandle == 0) {
                _log_e("BLE CCCD not found");
                metrics().addFail();
                _terminateConnection(error->status);
                return 0;
            }
            _subscribe(connHandle);
            return 0;
        }

        {
            const auto status = error->status;
            _log_e("BLE descriptor discovery failed: status=%d", status);
            metrics().addFail();
            _terminateConnection(status);
        }
        return 0;
    }

    int _handleSubscribed(uint16_t connHandle, const ble_gatt_error *error,
                          ble_gatt_attr * /*attribute*/) {
        if (!_sameConnection(connHandle)) {
            return 0;
        }
        if (error == nullptr || error->status != 0) {
            const auto status = error == nullptr ? -1 : error->status;
            _log_e("BLE subscribe failed: status=%d", status);
            metrics().addFail();
            _terminateConnection(status);
            return 0;
        }

        _setState(State::Subscribed);
        metrics().addSub();
        _activeDriver->onSubscribed({
            .connection = _connection,
            .attributeHandle = _characteristicHandle,
        });
        _log_i("BLE subscribed: conn=%u attr=%u", _connection.connHandle,
               _characteristicHandle);
        return 0;
    }

    int _handleNotify(const ble_gap_event &event) {
        const auto &notify = event.notify_rx;
        if (!_sameConnection(notify.conn_handle) ||
            notify.attr_handle != _characteristicHandle ||
            _activeDriver == nullptr) {
            return 0;
        }

        const auto payloadSize = OS_MBUF_PKTLEN(notify.om);
        if (payloadSize >
            StaticConfig::Bluetooth::maxNotificationPayloadBytes) {
            _log_w("BLE notification too large: %u", payloadSize);
            metrics().addDrop();
            return 0;
        }

        QueuedNotification queued{
            .driver = _activeDriver,
            .connection = _connection,
            .attributeHandle = notify.attr_handle,
            .indication = static_cast<bool>(notify.indication),
            .payloadSize = payloadSize,
        };
        const auto copyRet =
            os_mbuf_copydata(notify.om, 0, payloadSize, queued.payload.data());
        if (copyRet != 0) {
            _log_w("BLE notification copy failed: rc=%d", copyRet);
            metrics().addDrop();
            return 0;
        }

        auto pushRet = _sink.push(queued);
        if (!pushRet.ok()) {
            _log_w("BLE notification queue push failed: " ERR_FMT,
                   ERR_ARG(pushRet));
            metrics().addDrop();
            return 0;
        }
        metrics().addNotif();
        return 0;
    }

    [[nodiscard]] bool _sameConnection(uint16_t connHandle) const {
        return _connection.connHandle == connHandle && _activeDriver != nullptr;
    }

    void _terminateConnection(int reason) {
        if (!_connected()) {
            _clearConnection(true, reason);
            if (!_stopping) {
                _startScan();
            }
            return;
        }
        _setState(State::Disconnecting);
        const auto ret =
            ble_gap_terminate(_connection.connHandle,
                              BLE_ERR_REM_USER_CONN_TERM);
        if (ret != 0) {
            _log_w("BLE terminate failed: rc=%d", ret);
            _clearConnection(true, reason);
            if (!_stopping) {
                _startScan();
            }
        }
    }

    void _clearConnection(bool notifyDriver, int reason) {
        if (notifyDriver && _activeDriver != nullptr) {
            _activeDriver->onDisconnected(_connection, reason);
        }
        _activeDriver = nullptr;
        _activeDriverSlot = nullptr;
        _pendingDriver = nullptr;
        _pendingDriverSlot = nullptr;
        _connection = {};
        _pendingAddress = {};
        _pendingRssi = 0;
        _serviceStartHandle = 0;
        _serviceEndHandle = 0;
        _characteristicHandle = 0;
        _cccdHandle = 0;
        _serviceFound = false;
        _characteristicFound = false;
    }

    void _startScan() {
        if (_stopping || _config.driverCount == 0 ||
            _state == State::Scanning || _connected()) {
            return;
        }

        uint8_t ownAddrType{};
        const auto addrRet = ble_hs_id_infer_auto(0, &ownAddrType);
        if (addrRet != 0) {
            _log_e("BLE own address infer failed before scan: rc=%d", addrRet);
            metrics().addFail();
            return;
        }

        ble_gap_disc_params params{};
        params.filter_duplicates = _config.filterDuplicates ? 1U : 0U;
        params.passive = _config.activeScan ? 0U : 1U;
        params.itvl = _config.scanInterval;
        params.window = _config.scanWindow;
        params.filter_policy = 0;
        params.limited = 0;

        const auto ret = ble_gap_disc(ownAddrType, _config.scanDurationMs,
                                      &params, _gapEvent, this);
        if (ret != 0) {
            _log_e("BLE scan failed to start: rc=%d", ret);
            metrics().addFail();
            return;
        }

        _setState(State::Scanning);
        metrics().addScan();
        _log_i("BLE scan started");
    }

    Config _config{};
    NotificationSinkBinding _sink{};
    std::array<DriverSlot, StaticConfig::Bluetooth::maxDrivers> _drivers{};

    State _state = State::Idle;
    bool _initialized = false;
    bool _hostStarted = false;
    bool _stopping = false;

    IDeviceDriver *_pendingDriver = nullptr;
    DriverSlot *_pendingDriverSlot = nullptr;
    Address _pendingAddress{};
    int8_t _pendingRssi = 0;

    IDeviceDriver *_activeDriver = nullptr;
    DriverSlot *_activeDriverSlot = nullptr;
    ConnectionInfo _connection{};
    uint16_t _serviceStartHandle = 0;
    uint16_t _serviceEndHandle = 0;
    uint16_t _characteristicHandle = 0;
    uint16_t _cccdHandle = 0;
    bool _serviceFound = false;
    bool _characteristicFound = false;

    static inline NimbleCentral *_active = nullptr;

    static constexpr LogComponent logComponent = LogComponent::System;
};

} // namespace Totem::Bluetooth::detail::platform
