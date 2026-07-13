// IWYU pragma: private

#pragma once

#include "LoggingBackend/Interfaces/Types.hpp"
#include "Macros/Facade.hpp"
#include "Platform/PlatformSelect.hpp"
#include "Types/Error.hpp"
#include "Wifi/Interfaces/Config.hpp"
#include "Wifi/Interfaces/Types.hpp"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_wifi_types_generic.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>

namespace Totem::Wifi::detail::platform {

static constexpr LogComponent logComponent = LogComponent::System;

class PlatformESP32 {
  public:
    DELETE_COPY(PlatformESP32)
    DELETE_MOVE(PlatformESP32)

    PlatformESP32() = default;
    ~PlatformESP32() { (void)end(); }

    ReturnCode begin(const Config &config, std::string_view password) {
        if (_initialized || _netif != nullptr) {
            return ERR(LifecycleError, Active);
        }

        _config = config;
        resetStatus();
        _statusMode.store(config.mode);

        if (config.mode == Mode::Disabled) {
            _log_i("WiFi disabled by config");
            return OK();
        }

        auto ret = beginEnabled(password);
        if (!ret.ok()) {
            const auto cleanup = end();
            if (!cleanup.ok()) {
                _log_w("WiFi cleanup after failed begin returned " ERR_FMT,
                       ERR_ARG(cleanup));
            }
            return ret;
        }

        _initialized = true;
        _started.store(true);
        _log_i("WiFi started in %s mode",
               config.mode == Mode::Station ? "station" : "access-point");
        return OK();
    }

    ReturnCode end() {
        auto ret = OK();

        if (_started.exchange(false)) {
            ret.combine(::platform::map_platform_error(esp_wifi_stop()));
        }

        if (_wifiEventHandler != nullptr) {
            ret.combine(::platform::map_platform_error(
                esp_event_handler_instance_unregister(
                    WIFI_EVENT, ESP_EVENT_ANY_ID, _wifiEventHandler)));
            _wifiEventHandler = nullptr;
        }

        if (_ipEventHandler != nullptr) {
            ret.combine(::platform::map_platform_error(
                esp_event_handler_instance_unregister(
                    IP_EVENT, ESP_EVENT_ANY_ID, _ipEventHandler)));
            _ipEventHandler = nullptr;
        }

        if (_initialized) {
            ret.combine(::platform::map_platform_error(esp_wifi_deinit()));
            _initialized = false;
        }

        if (_netif != nullptr) {
            esp_netif_destroy_default_wifi(std::exchange(_netif, nullptr));
        }

        resetStatus();
        _statusMode.store(Mode::Disabled);
        return ret;
    }

    [[nodiscard]] Status status() const {
        return {
            .mode = _statusMode.load(),
            .started = _started.load(),
            .accessPointStarted = _accessPointStarted.load(),
            .accessPointStationCount = _accessPointStationCount.load(),
            .stationConnected = _stationConnected.load(),
            .stationIpv4 =
                {
                    .raw = _stationIpv4Raw.load(),
                    .valid = _stationHasIpv4.load(),
                },
        };
    }

  private:
    static ReturnCode mapAlreadyInitialized(esp_err_t err) {
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            return OK();
        }
        return ::platform::map_platform_error(err);
    }

    ReturnCode beginEnabled(std::string_view password) {
        FAIL_IF_ERR_FWD(mapAlreadyInitialized(esp_netif_init()),
                        "Failed to initialize esp-netif");
        FAIL_IF_ERR_FWD(mapAlreadyInitialized(esp_event_loop_create_default()),
                        "Failed to create default ESP event loop");

        _netif = _config.mode == Mode::Station
                     ? esp_netif_create_default_wifi_sta()
                     : esp_netif_create_default_wifi_ap();
        FAIL_IF_NULL(_netif, ERR(CoreError, OperationFailed),
                     "Failed to create default WiFi network interface");

        FAIL_IF_PLATFORM_FWD(
            esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &PlatformESP32::onWifiEvent,
                                                this, &_wifiEventHandler),
            "Failed to register WiFi event handler");
        FAIL_IF_PLATFORM_FWD(
            esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                                &PlatformESP32::onIpEvent, this,
                                                &_ipEventHandler),
            "Failed to register IP event handler");

        wifi_init_config_t initConfig = WIFI_INIT_CONFIG_DEFAULT();
        initConfig.nvs_enable = _config.disableNvsStorage ? 0 : 1;
        FAIL_IF_PLATFORM_FWD(esp_wifi_init(&initConfig),
                             "Failed to initialize ESP WiFi");

        if (_config.disableNvsStorage) {
            FAIL_IF_PLATFORM_FWD(esp_wifi_set_storage(WIFI_STORAGE_RAM),
                                 "Failed to select RAM-only WiFi storage");
        }

        const auto driverMode =
            _config.mode == Mode::Station ? WIFI_MODE_STA : WIFI_MODE_AP;
        FAIL_IF_PLATFORM_FWD(esp_wifi_set_mode(driverMode),
                             "Failed to select WiFi mode");

        if (_config.mode == Mode::Station) {
            FAIL_IF_PLATFORM_FWD(esp_wifi_set_ps(WIFI_PS_NONE),
                                 "Failed to disable station WiFi power save");
            FAIL_IF_PLATFORM_FWD(
                esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20),
                "Failed to select station WiFi bandwidth");
        }

        FAIL_IF_ERR_FWD(configureDriver(password),
                        "Failed to configure WiFi driver");
        FAIL_IF_PLATFORM_FWD(esp_wifi_start(), "Failed to start WiFi driver");
        setMaxTxPowerBestEffort();
        return OK();
    }

    ReturnCode configureDriver(std::string_view password) {
        wifi_config_t driverConfig{};
        if (_config.mode == Mode::Station) {
            copyBytes(driverConfig.sta.ssid, _config.station->credentials.ssid);
            copyBytes(driverConfig.sta.password, password);
            driverConfig.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
            driverConfig.sta.pmf_cfg.capable = true;
            driverConfig.sta.pmf_cfg.required = false;
            FAIL_IF_PLATFORM_FWD(
                esp_wifi_set_config(WIFI_IF_STA, &driverConfig),
                "Failed to apply station WiFi config");
            return OK();
        }

        const auto ssidLength =
            stringLength(_config.accessPoint->credentials.ssid);
        const auto passwordLength = password.size();
        copyBytes(driverConfig.ap.ssid, _config.accessPoint->credentials.ssid);
        copyBytes(driverConfig.ap.password, password);
        driverConfig.ap.ssid_len = static_cast<uint8_t>(ssidLength);
        driverConfig.ap.channel = _config.accessPoint->channel;
        driverConfig.ap.ssid_hidden = _config.accessPoint->hidden ? 1U : 0U;
        driverConfig.ap.max_connection = _config.accessPoint->maxConnections;
        driverConfig.ap.authmode =
            passwordLength == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        driverConfig.ap.pmf_cfg.capable = true;
        driverConfig.ap.pmf_cfg.required = false;
        FAIL_IF_PLATFORM_FWD(esp_wifi_set_config(WIFI_IF_AP, &driverConfig),
                             "Failed to apply AP WiFi config");
        return OK();
    }

    template <std::size_t Size>
    static void copyBytes(uint8_t (&out)[Size], const char *value) {
        if (value == nullptr) {
            return;
        }
        const auto length = stringLength(value);
        const auto bytesToCopy = length < Size ? length : Size;
        std::memcpy(out, value, bytesToCopy);
    }

    template <std::size_t Size>
    static void copyBytes(uint8_t (&out)[Size], std::string_view value) {
        const auto bytesToCopy = value.size() < Size ? value.size() : Size;
        std::memcpy(out, value.data(), bytesToCopy);
    }

    static std::size_t stringLength(const char *value) {
        if (value == nullptr) {
            return 0;
        }
        return std::strlen(value);
    }

    void resetStatus() {
        _started.store(false);
        _accessPointStarted.store(false);
        _accessPointStationCount.store(0);
        _stationConnected.store(false);
        _stationHasIpv4.store(false);
        _stationIpv4Raw.store(0);
        _stationReconnectAttempts.store(0);
    }

    static void onWifiEvent(void *arg, esp_event_base_t /*eventBase*/,
                            int32_t eventId, void *eventData) {
        auto *self = static_cast<PlatformESP32 *>(arg);
        if (self == nullptr) {
            return;
        }
        self->handleWifiEvent(eventId, eventData);
    }

    void handleWifiEvent(int32_t eventId, void *eventData) {
        switch (eventId) {
        case WIFI_EVENT_AP_START:
            _accessPointStarted.store(true);
            _log_i("WiFi AP started");
            break;
        case WIFI_EVENT_AP_STOP:
            _accessPointStarted.store(false);
            _accessPointStationCount.store(0);
            _log_i("WiFi AP stopped");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            onApStationConnected(eventData);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            onApStationDisconnected(eventData);
            break;
        case WIFI_EVENT_STA_START:
            _log_i("WiFi station started, connecting");
            (void)esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            _stationConnected.store(true);
            _stationReconnectAttempts.store(0);
            _log_i("WiFi station connected");
            startDhcpClient();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            onStationDisconnected(eventData);
            break;
        case WIFI_EVENT_STA_STOP:
            _stationConnected.store(false);
            _stationHasIpv4.store(false);
            _stationIpv4Raw.store(0);
            _log_i("WiFi station stopped");
            break;
        default:
            break;
        }
    }

    void onApStationConnected(void *eventData) {
        const auto current = _accessPointStationCount.load();
        if (current < UINT8_MAX) {
            _accessPointStationCount.store(current + 1U);
        }

        auto *event = static_cast<wifi_event_ap_staconnected_t *>(eventData);
        if (event == nullptr) {
            _log_i("WiFi AP station connected; stations=%u",
                   _accessPointStationCount.load());
            return;
        }

        _log_i("WiFi AP station connected aid=%u stations=%u",
               static_cast<unsigned>(event->aid),
               static_cast<unsigned>(_accessPointStationCount.load()));
    }

    void onApStationDisconnected(void *eventData) {
        const auto current = _accessPointStationCount.load();
        if (current > 0) {
            _accessPointStationCount.store(current - 1U);
        }

        auto *event = static_cast<wifi_event_ap_stadisconnected_t *>(eventData);
        if (event == nullptr) {
            _log_i("WiFi AP station disconnected; stations=%u",
                   _accessPointStationCount.load());
            return;
        }

        _log_i("WiFi AP station disconnected aid=%u stations=%u",
               static_cast<unsigned>(event->aid),
               static_cast<unsigned>(_accessPointStationCount.load()));
    }

    void onStationDisconnected(void *eventData) {
        _stationConnected.store(false);
        _stationHasIpv4.store(false);
        _stationIpv4Raw.store(0);

        auto *event = static_cast<wifi_event_sta_disconnected_t *>(eventData);
        if (event == nullptr) {
            _log_w("WiFi station disconnected");
        } else {
            _log_w("WiFi station disconnected reason=%u rssi=%d",
                   static_cast<unsigned>(event->reason),
                   static_cast<int>(event->rssi));
        }

        if (!_config.station->reconnect || !_started.load()) {
            return;
        }

        auto attempts = _stationReconnectAttempts.load();
        if (_config.station->maxReconnectAttempts != 0 &&
            attempts >= _config.station->maxReconnectAttempts) {
            _log_w("WiFi station reconnect limit reached");
            return;
        }

        _stationReconnectAttempts.store(static_cast<uint8_t>(attempts + 1U));
        _log_i("WiFi station reconnect attempt %u",
               static_cast<unsigned>(_stationReconnectAttempts.load()));
        (void)esp_wifi_connect();
    }

    void startDhcpClient() {
        if (_netif == nullptr) {
            return;
        }
        const auto ret = esp_netif_dhcpc_start(_netif);
        if (ret == ESP_OK || ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            return;
        }
        _log_w("Failed to start WiFi DHCP client: " ERR_FMT,
               ERR_ARG(::platform::map_platform_error(ret)));
    }

    void setMaxTxPowerBestEffort() {
        const auto ret = esp_wifi_set_max_tx_power(maxTxPowerQuarterDbm);
        if (ret == ESP_OK) {
            return;
        }
        _log_w("Failed to select WiFi TX power: " ERR_FMT,
               ERR_ARG(::platform::map_platform_error(ret)));
    }

    static void onIpEvent(void *arg, esp_event_base_t /*eventBase*/,
                          int32_t eventId, void *eventData) {
        auto *self = static_cast<PlatformESP32 *>(arg);
        if (self == nullptr) {
            return;
        }
        self->handleIpEvent(eventId, eventData);
    }

    void handleIpEvent(int32_t eventId, void *eventData) {
        switch (eventId) {
        case IP_EVENT_STA_GOT_IP: {
            auto *event = static_cast<ip_event_got_ip_t *>(eventData);
            if (event == nullptr) {
                return;
            }
            _stationIpv4Raw.store(event->ip_info.ip.addr);
            _stationHasIpv4.store(true);
            _stationReconnectAttempts.store(0);
            _log_i("WiFi station got IP " IPSTR, IP2STR(&event->ip_info.ip));
            break;
        }
        case IP_EVENT_STA_LOST_IP:
            _stationHasIpv4.store(false);
            _stationIpv4Raw.store(0);
            _log_w("WiFi station lost IP");
            break;
        default:
            break;
        }
    }

    Config _config{};
    static constexpr int8_t maxTxPowerQuarterDbm = 84;
    esp_netif_t *_netif = nullptr;
    esp_event_handler_instance_t _wifiEventHandler = nullptr;
    esp_event_handler_instance_t _ipEventHandler = nullptr;
    bool _initialized = false;
    std::atomic<Mode> _statusMode{Mode::Disabled};
    std::atomic<bool> _started{false};
    std::atomic<bool> _accessPointStarted{false};
    std::atomic<uint8_t> _accessPointStationCount{0};
    std::atomic<bool> _stationConnected{false};
    std::atomic<bool> _stationHasIpv4{false};
    std::atomic<uint32_t> _stationIpv4Raw{0};
    std::atomic<uint8_t> _stationReconnectAttempts{0};
};

} // namespace Totem::Wifi::detail::platform
