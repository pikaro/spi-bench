#pragma once

#include "Types/Uart.hpp"

constexpr auto UartConsoleConfig = UartConfig{
    .baudRate = BaudRate::Baud115200,
    .uartNumber = 0,
    .txBufferSize = 2048,
    .maxReadLen = 128,
};
