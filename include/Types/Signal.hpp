#pragma once

#include <cstdint>

enum class Signal : uint8_t {
    Unknown = 0,
    Ping,
    None,
    Success,
    Failure,
    Timeout,
    Invalid,
    Ready,
    Start,
    Stop,
    Over,
    Under,
    Min,
    Max,
    UartData,
    UartOverflow,
    UartError,
    Rs485Attention,
    SpiAttention,
    SpiTransfer,
};

struct SignalWaitResult {
    Signal signal = Signal::Unknown;
    bool timeout = false;
    bool ok = false;
};
