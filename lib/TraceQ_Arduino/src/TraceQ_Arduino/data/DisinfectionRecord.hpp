#pragma once

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct DisinfectionRecord
{
    int           MachineNumber{};
    LocalDateTime DateTime{};

    DisinfectionRecord() = default;
    DisinfectionRecord(int n, const LocalDateTime &dt) : MachineNumber(n), DateTime(dt) {}
};

struct DisinfectionDetail
{
    LocalDateTime DateTime{};
    int           GroupNumber{1};

    DisinfectionDetail() = default;
    explicit DisinfectionDetail(const LocalDateTime &dt) : DateTime(dt) {}
    explicit DisinfectionDetail(int n) : GroupNumber(n) {}
    DisinfectionDetail(const LocalDateTime &dt, int n) : DateTime(dt), GroupNumber(n) {}
};
static_assert(sizeof(DisinfectionRecord) == 10, "DisinfectionRecord must be the 10 tag bytes (number 2 + datetime 8)");
static_assert(sizeof(DisinfectionDetail) == 10, "DisinfectionDetail must be the 10 tag bytes (datetime 8 + group 2)");
