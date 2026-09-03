#include "GpioSignalTest/detail/Timing.hpp"

using namespace Totem::GpioSignalTest::detail::timing;

static_assert(periodUs(0) == 0);
static_assert(periodUs(7) == 142'857);
static_assert(highTimeUs(periodUs(7), 500) == 71'428);
static_assert(periodUs(7) - highTimeUs(periodUs(7), 500) == 71'429);
static_assert(frequencyMilliHz(periodUs(7)) == 7'000);
static_assert(dutyPartsPerThousand(50'000, 50'000) == 500);
static_assert(dutyPartsPerThousand(25'000, 75'000) == 250);
static_assert(absoluteDifference(7'010, 7'000) == 10);
static_assert(errorPartsPerThousand(7'140, 7'000) == 20);

int main() { return 0; }
