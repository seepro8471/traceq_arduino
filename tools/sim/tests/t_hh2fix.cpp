// HH2 수정안 검증 — 레거시 시각 동기 **실패**를 실패음(짧게 4회)으로.
// HEAD 에서는 빨강(성공 `updated` 와 같은 1000ms 1회) · m_fix 에서 초록.
#include "hh2_sound.h"

static void raw(const char *s) { serial_inject(s, strlen(s)); GUARDED(serialEvent()); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('S');
    raw("Z"); run_loops(1);

    char ok[48], bad[48];
    snprintf(ok,  sizeof(ok),  "T%u;%u;%u;3;11;22;33;",
             (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
    snprintf(bad, sizeof(bad), "T%u;13;%u;3;11;44;55;",
             (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_DAY);

    snd_begin(); raw(ok);
    const uint8_t okP1000 = buzz_count(1000), okP100 = buzz_count(100);
    const bool okSet = (rtc.GetCurrentDateTime().minute() == 22);
    snd_show("성공(시계 바뀜)");

    snd_begin(); raw(bad);
    const uint8_t badP1000 = buzz_count(1000), badP100 = buzz_count(100);
    const bool badKept = (rtc.GetCurrentDateTime().minute() == 22);
    snd_show("실패(시계 안바뀜)");

    tlog("  성공 1000x%u 100x%u 반영=%d / 실패 1000x%u 100x%u 유지=%d\n",
         okP1000, okP100, (int)okSet, badP1000, badP100, (int)badKept);

    CHECK(okSet && badKept, "회귀: 성공은 시계를 바꾸고 실패는 그대로 둔다");
    CHECK(okP1000 == 1, "회귀: 시각 동기 성공은 안내음(1000ms 1회) 그대로");
    CHECK(badP100 == 4 && badP1000 == 0,
          "★시각 동기 실패는 실패음(짧게 4회)이다 — 성공과 소리가 다르다");
    CHECK(!(okP1000 == badP1000 && okP100 == badP100),
          "★성공과 실패의 소리가 같지 않다(사람이 귀로 갈릴 수 있다)");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
