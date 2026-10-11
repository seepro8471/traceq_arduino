#pragma once

#include <stddef.h>

#include "TraceQ_Arduino/data/LocalDateTime.hpp"

constexpr unsigned long TRACEQ_COMPANY_CODE{4088153715UL};

/**
 * 1.0과 동일 레이아웃 (총 10 byte on AVR, int=2byte).
 *   0..3  CompanyCode (uint32 LE)
 *   4..7  RegisteredDate (Year uint16 LE, Month uint8, Day uint8)
 *   8..9  TagType (int16 LE) — SCOPE/MANAGER/CLEAR 값을 담음
 */
struct Company
{
    unsigned long CompanyCode{};
    LocalDate     RegisteredDate{};
    int           TagType{};

    Company() = default;
};
static_assert(sizeof(Company) == 10 && offsetof(Company, CompanyCode) == 0 && offsetof(Company, RegisteredDate) == 4 && offsetof(Company, TagType) == 8,
              "Company must be the 10 tag bytes (code@0 date@4 type@8)");
