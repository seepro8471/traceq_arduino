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
