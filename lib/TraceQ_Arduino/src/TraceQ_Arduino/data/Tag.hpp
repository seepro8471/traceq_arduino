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
static_assert(sizeof(Tag) == 16 && sizeof(TagSerial) == 16, "Tag/TagSerial must be one 16-byte block each");
