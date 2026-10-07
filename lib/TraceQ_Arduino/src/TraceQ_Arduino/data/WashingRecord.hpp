#pragma once

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct WashingRecord
{
    int           MachineNumber{};
    LocalDateTime DateTime{};

    WashingRecord() = default;
    WashingRecord(int mn, const LocalDateTime &dt) : MachineNumber(mn), DateTime(dt) {}
};
static_assert(sizeof(WashingRecord) == 10, "WashingRecord must be the 10 tag bytes (number 2 + datetime 8)");
