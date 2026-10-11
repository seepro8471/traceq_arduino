#pragma once

#include <stddef.h>

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

struct Gateway
{
    int           Number{};
    LocalDateTime DateTime{};

    Gateway() = default;
    Gateway(int n, const LocalDateTime &dt) : Number(n), DateTime(dt) {}
};
static_assert(sizeof(Gateway) == 10 && offsetof(Gateway, Number) == 0 && offsetof(Gateway, DateTime) == 2, "Gateway must be the 10 tag bytes (number@0 datetime@2)");
