#pragma once

#include "StatusLed/Interfaces/Types.hpp"
#include "Types/Error.hpp"
#include <atomic>

namespace Totem::StatusLed::detail {

struct IService {
    virtual ~IService() = default;

    virtual Totem::StatusLed::Directory directory() = 0;
    virtual ReturnCode setCoreReady() = 0;
    virtual ReturnCode setTargetsReady() = 0;
    virtual ReturnCode setOff() = 0;
    virtual ReturnCode recordUnhandledError() = 0;
    virtual ReturnCode recordCritical() = 0;
    virtual ReturnCode work(uint32_t nowMs) = 0;
};

struct NullService : public IService {
    Totem::StatusLed::Directory directory() override { return {}; }
    ReturnCode setCoreReady() override {
        return ReturnCode::from(CoreError::Ok);
    }
    ReturnCode setTargetsReady() override {
        return ReturnCode::from(CoreError::Ok);
    }
    ReturnCode setOff() override { return ReturnCode::from(CoreError::Ok); }
    ReturnCode recordUnhandledError() override {
        return ReturnCode::from(CoreError::Ok);
    }
    ReturnCode recordCritical() override {
        return ReturnCode::from(CoreError::Ok);
    }
    ReturnCode work(uint32_t /*unused*/) override {
        return ReturnCode::from(CoreError::Ok);
    }
};

inline NullService nullService{};

} // namespace Totem::StatusLed::detail

class StatusLedService {
    using IService = Totem::StatusLed::detail::IService;

  public:
    static void set(IService &backend) {
        _backend.store(&backend, std::memory_order_release);
    }

    static Totem::StatusLed::Directory directory() {
        return get().directory();
    }

    static ReturnCode setCoreReady() { return get().setCoreReady(); }
    static ReturnCode setTargetsReady() { return get().setTargetsReady(); }
    static ReturnCode setOff() { return get().setOff(); }
    static ReturnCode recordUnhandledError() {
        return get().recordUnhandledError();
    }
    static ReturnCode recordCritical() { return get().recordCritical(); }
    static ReturnCode work(uint32_t nowMs) { return get().work(nowMs); }

  private:
    static IService &get() {
        return *_backend.load(std::memory_order_acquire);
    }

    static inline std::atomic<IService *> _backend{
        &Totem::StatusLed::detail::nullService};
};
