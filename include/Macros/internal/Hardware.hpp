#pragma once

#define PIN(hwType, pinName) static_cast<uint8_t>(CONCAT(hwType, Pin)::pinName)
