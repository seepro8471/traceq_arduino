#pragma once

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct Gateway
{
    int           Number{};
    LocalDateTime DateTime{};

    Gateway() = default;
    Gateway(int n, const LocalDateTime &dt) : Number(n), DateTime(dt) {}
};
