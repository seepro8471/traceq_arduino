#pragma once

#include <stddef.h>

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct WashingRecord
{
    int           MachineNumber{};
    LocalDateTime DateTime{};

    WashingRecord() = default;
    WashingRecord(int mn, const LocalDateTime &dt) : MachineNumber(mn), DateTime(dt) {}
};
static_assert(sizeof(WashingRecord) == 10, "WashingRecord must be the 10 tag bytes (number 2 + datetime 8)");
static_assert(offsetof(WashingRecord, MachineNumber) == 0 && offsetof(WashingRecord, DateTime) == 2, "WashingRecord layout: number@0 datetime@2");
