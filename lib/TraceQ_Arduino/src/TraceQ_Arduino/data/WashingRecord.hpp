#pragma once

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct WashingRecord
{
    int           MachineNumber{};
    LocalDateTime DateTime{};

    WashingRecord() = default;
    WashingRecord(int mn, const LocalDateTime &dt) : MachineNumber(mn), DateTime(dt) {}
};
