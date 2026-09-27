#pragma once
// HH2 공용 — 한 조작의 **소리(펄스 길이 x 횟수)**와 **2행 글자가 남은 시간**을 실측해 한 줄로 적는다.
#include "common.h"

static const uint16_t kPulse[] = {30, 40, 50, 60, 100, 150, 300, 400, 500, 600, 1000};

static void snd_begin()
{
    logs_clear();
    buzz_clear();
    dwell_clear();
}

// label | 부저 패턴 | 글자가 남은 시간과 내용
static void snd_show(const char *label)
{
    char b[40];
    b[0] = 0;
    int n = 0;
    for (uint8_t i = 0; i < sizeof(kPulse) / sizeof(kPulse[0]); ++i)
    {
        const uint8_t c = buzz_count(kPulse[i]);
        if (c && n < 32) n += snprintf(b + n, sizeof(b) - n, "%ux%u ", (unsigned)kPulse[i], (unsigned)c);
    }
    if (n == 0) snprintf(b, sizeof(b), "-무음-");
    tlog("  %-18s|%-16s|", label, b);
    if (dwell_count() == 0) tlog(" (안지움)");
    for (uint8_t i = 0; i < dwell_count(); ++i)
        tlog(" %lu\"%.13s\"", (unsigned long)dwell_ms(i), dwell_text(i));
    tlog("\n");
}

// 소리 이름 — 사장님 규칙(09-23)에 맞춰 판정한다
static bool snd_short1() { return buzz_count(50) == 1 && buzz_count(100) == 0 && buzz_count(400) == 0 && buzz_count(600) == 0; }
static bool snd_fail4()  { return buzz_count(100) == 4 && buzz_count(50) == 0 && buzz_count(400) == 0 && buzz_count(600) == 0; }
static bool snd_long2()  { return buzz_count(400) == 2 && buzz_count(50) == 0 && buzz_count(100) == 0 && buzz_count(600) == 0; }
static bool snd_reject() { return buzz_count(60) == 2 && buzz_count(600) == 1 && buzz_count(50) == 0 && buzz_count(100) == 0 && buzz_count(400) == 0; }
static bool snd_silent()
{
    for (uint8_t i = 0; i < sizeof(kPulse) / sizeof(kPulse[0]); ++i)
        if (buzz_count(kPulse[i])) return false;
    return true;
}
static uint32_t snd_dwell_of(const char *text)
{
    for (uint8_t i = 0; i < dwell_count(); ++i)
        if (strstr(dwell_text(i), text) != nullptr) return dwell_ms(i);
    return 0;
}
