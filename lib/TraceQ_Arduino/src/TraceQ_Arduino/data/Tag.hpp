#pragma once

#include <stddef.h>
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
static_assert(offsetof(Tag, Number) == 0 && offsetof(Tag, ID) == 2, "Tag layout: number@0 id@2");
