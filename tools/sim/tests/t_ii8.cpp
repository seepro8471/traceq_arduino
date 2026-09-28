// II-H 제안 잠금 1 — 안 잠긴 봉합 둘을 행위로 잠근다(HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다).
//  A) v2.2.30 HH2 P2-1 의 형제 셋: 레거시 시각 동기 실패음 100x4 는 t_hh2fix·t_hh2s 가 **범위 검사 자리(월 13)만** 본다.
//     칸 부족(sep 없음) · 칸 경계 255 초과 · isValid 실패(2월 30일) 세 자리는 소리를 1000x1 로 되돌려도 772/772 초록이었다.
//  B) v2.2.25 P2 "GatewaySerialEvent(cmd)": t_dd1 (1b) 는 (1a) 와 같은 G1(7)을 쓰는데 v2.2.31 이 그 7 을 설정값에
//     저장하므로 buffer 로 되돌려도 폴백이 7 을 내 **헛초록**이 됐다 → 재기동 뒤 설정값을 다른 번호(11)로 두고 본다.
#include "common.h"

static SimCard b;

static void raw(const char *s, size_t n) { serial_inject(s, n); GUARDED(serialEvent()); run_loops(1); }

static void pump(uint32_t ms)
{
    const uint32_t end = g_ms + ms;
    while (g_ms < end)
    {
        GUARDED(loop());
        if (Serial.available() > 0) GUARDED(serialEvent());
        sim_advance_ms(20);
    }
}

static int gate_of(const SimCard &c)
{
    int n = 0;
    memcpy(&n, c.data[SECTOR1_GATEWAY], 2);
    return n;
}

// 실패 한 건: 소리 100x4 · 1000 없음 · 시계 불변
static bool fail4_kept(const char *cmd, size_t n, const char *label)
{
    const DateTime before = rtc.GetCurrentDateTime();
    logs_clear();
    buzz_clear();
    raw(cmd, n);
    const uint8_t p100 = buzz_count(100), p1000 = buzz_count(1000);
    const DateTime after = rtc.GetCurrentDateTime();
    const bool kept = after.hour() == before.hour() && after.minute() == before.minute();
    tlog("  %s: 100x%u 1000x%u 시계유지=%d InvalidDateTime=%d\n", label, p100, p1000, (int)kept,
         (int)lcd_has("Invalid DateTime"));
    return p100 == 4 && p1000 == 0 && kept && lcd_has("Invalid DateTime");
}

int main()
{
    // ── A) 레거시 시각 동기 실패음 — 네 실패 자리 전부 ──
    rtc_set(rel_date(10, 0, 0));
    boot('S');
    raw("Z", 1);
    {
        char a[48];
        snprintf(a, sizeof(a), "T%u;%u;%u;3;14;", (unsigned)TRACEQ_RELEASE_YEAR,
                 (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);      // 칸 부족 → sep 없음
        CHECK(fail4_kept(a, strlen(a), "A1 칸부족"),
              "A1 레거시 T 칸 부족(sep 없음) 실패는 실패음 100x4 — 성공음 1000x1 이 아니다");

        char big[300];
        int k = snprintf(big, sizeof(big), "T%u;%u;%u;3;14;30;", (unsigned)TRACEQ_RELEASE_YEAR,
                         (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        memset(big + k, '0', 250);                                                  // 초 칸이 255 를 넘는다
        big[k + 250] = ';';
        CHECK(fail4_kept(big, k + 251, "A2 칸경계>254"),
              "A2 레거시 T 칸 경계 255 초과 실패는 실패음 100x4");

        char c[48];
        snprintf(c, sizeof(c), "T%u;2;30;3;14;30;0;", (unsigned)TRACEQ_RELEASE_YEAR);   // 범위는 통과 · isValid 거짓
        CHECK(fail4_kept(c, strlen(c), "A3 2월30일"),
              "A3 레거시 T 날짜 무효(2월 30일 · isValid 거짓) 실패는 실패음 100x4");
    }

    // ── B) 'Z' 가 붙은 전문의 G1 — 설정값이 다른 번호일 때 ──
    {
        deviceOption.SetType('G');
        hard_reset(false, 2);                          // mGateNumber = -1
        deviceOption.SetNumber(11);                    // 설정값은 PC 번호(7)와 다르다
        sim_advance_ms(60UL * 1000);
        const char p2[] = "ZG17;G22026;9;23;3;12;15;0;G3PT0002;NAMEB;;G4SUBJB;;;G5;";
        serial_inject(p2, sizeof(p2) - 1);
        pump(3000);
        make_tag(b, 0x71, SCOPE_TYPE_TAG, 71, "SC", "SER");
        set_process(b, Process{0, 0, 0, 0, 0, false, 0, 0});
        logs_clear();
        touch(b);
        tlog("  B 'Z'+G17 · 설정값=%d · 태그 본체번호=%d · GateNumber=%d\n",
             deviceOption.GetNumber(), gate_of(b), gatewayProcessor.GateNumber());
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "PT0002", 6) == 0, "B 전제: 'Z' 가 붙은 전문도 그 환자로 기록된다");
        CHECK(gate_of(b) == 7 && gatewayProcessor.GateNumber() == 7,
              "B 'Z' 가 붙은 전문의 G1(7)이 태그에 기록된다 — 설정값(11) 폴백이 아니다");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
