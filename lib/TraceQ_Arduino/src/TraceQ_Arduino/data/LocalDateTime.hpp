#pragma once

#include <stdint.h>

struct LocalDate
{
    uint16_t Year{};
    uint8_t  Month{};
    uint8_t  Day{};

    LocalDate() = default;
    LocalDate(uint16_t y, uint8_t m, uint8_t d) : Year(y), Month(m), Day(d) {}
};

struct LocalTime
{
    uint8_t Hour{};
    uint8_t Minute{};
    uint8_t Second{};

    LocalTime() = default;
    LocalTime(uint8_t h, uint8_t m, uint8_t s) : Hour(h), Minute(m), Second(s) {}
};

struct LocalDateTime
{
    LocalDate Date{};
    uint8_t   DayOfWeek{0}; // deprecated 필드. 레이아웃 호환을 위해 유지.
    LocalTime Time{};

    LocalDateTime() = default;
    LocalDateTime(const LocalDate &d, const LocalTime &t) : Date(d), Time(t) {}
    LocalDateTime(const LocalDate &d, uint8_t dow, const LocalTime &t)
        : Date(d), DayOfWeek(dow), Time(t) {}
};
// 태그 레코드·EEPROM(교환일 169~176)이 이 8바이트 레이아웃에 기댄다 — 크기만 잠근다(AVR: 패딩 없음 · 17차)
static_assert(sizeof(LocalDateTime) == 8, "LocalDateTime must be 8 bytes (year 2 + month/day 2 + dow 1 + h/m/s 3)");
