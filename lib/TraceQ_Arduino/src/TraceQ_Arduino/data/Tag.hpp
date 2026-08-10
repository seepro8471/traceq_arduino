#pragma once

#include <stdint.h>

struct Tag
{
    int           Number{};
    unsigned char ID[14]{};
    Tag() = default;
};

struct TagSerial
{
    unsigned char Serial[16]{};
    TagSerial() = default;
};
