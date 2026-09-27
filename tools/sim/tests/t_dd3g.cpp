// DD3 — v2.2.23~24 의 조각 이어 붙이기(최대 3회 · 회당 1초 블로킹)가 PC 명령·타이밍에 주는 영향(게이트웨이 G).
//  ① 이음매 길이별로 살아나는가 ② serialEvent 한 번이 붙잡는 시간 ③ 그 사이 도착한 비-게이트웨이 명령의 운명
//  ★시각은 가짜 millis()(호출마다 +1ms) 기준이라 절대값이 아니라 **갈래 사이의 비교**로만 쓴다.
#include "common.h"

// 출시일 기준 시각 — 절대 날짜를 쓰면 출시일이 움직일 때 뜻이 조용히 바뀐다(IsUnsynced 비교 기준이 출시일).
static inline DateTime rel_date2(uint8_t h, uint8_t m, uint8_t s) { return rel_date(h, m, s); }
static_assert(TRACEQ_RELEASE_YEAR == 2026, "덤프 블록 줄의 연도 hex(EA07) 전제 — 연도가 바뀌면 이 시험의 기대값을 고칠 것");

static SimCard sco;

// 델파이는 `MonitoringMemo.Lines[Count-2]`(마지막 실제 줄)만 보고 ScopeTagAnalysis 를 부른다
// (MainFormSo.pas 32,551줄 · 5756줄) — 게이트웨이 전문의 마지막 줄은 반드시 `Sm!` 여야 한다.
static char s_last[64];
static const char *last_line()
{
    const char *p = g_serialOut;
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n')) --n;
    size_t b = n;
    while (b > 0 && p[b - 1] != '\n') --b;
    size_t len = n - b;
    if (len >= sizeof(s_last)) len = sizeof(s_last) - 1;
    memcpy(s_last, p + b, len);
    s_last[len] = 0;
    for (char *q = s_last; *q; ++q) if (*q == '\r') { *q = 0; break; }
    return s_last;
}

static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void drain()
{
    while (Serial.available() > 0) (void)Serial.read();
}

// 이음매 gap ms 로 올눈식 두 조각을 보내고, 환자가 기록됐는지와 serialEvent 블로킹 시간을 돌려준다.
static bool seam_case(uint32_t gap, uint8_t uid, const char *key, uint32_t *blockMs)
{
    sim_advance_ms(60UL * 1000);
    drain();
    char c1[80];
    snprintf(c1, sizeof(c1), "G10000;G22026;9;27;0;11;20;0;G3%s;NAMEX;;G4SUBJX;;;", key);
    const char c2[] = "G52000;01;01;M;";
    const uint32_t t0 = g_ms + 10;
    serial_queue(c1, strlen(c1), t0);
    if (gap > 0) serial_queue(c2, sizeof(c2) - 1, t0 + gap);
    else { /* 한 조각 — 꼬리까지 같은 조각에 */ }
    sim_advance_ms(20);
    const uint32_t t = g_ms;
    GUARDED(serialEvent());
    *blockMs = g_ms - t;
    fresh_scope(sco, uid, uid);
    logs_clear();
    touch(sco);
    return memcmp(sco.data[SECTOR2_PATIENT_KEY], key, strlen(key)) == 0;
}

int main()
{
    rtc_set(rel_date2(10, 0, 0));
    boot('G');
    drain();

    // ── B1 온전한 전문(세척관리 실제 형식, 한 번의 write) ──
    uint32_t dOne = 0;
    {
        const char p[] = "G1G22026;9;27;0;10;5;0;G3AAA001;NAMEA;G4SUBJA;;;G5";
        serial_inject(p, sizeof(p) - 1);
        const uint32_t t = g_ms;
        GUARDED(serialEvent());
        dOne = g_ms - t;
        fresh_scope(sco, 0x70, 70);
        logs_clear();
        touch(sco);
        tlog("  B1 온전(한 조각): serialEvent %lu ms · 키=%.8s · 마지막줄=[%s]\n",
             (unsigned long)dOne, (const char *)sco.data[SECTOR2_PATIENT_KEY], last_line());
        CHECK(memcmp(sco.data[SECTOR2_PATIENT_KEY], "AAA001", 6) == 0,
              "B1 온전한 전문은 그대로 기록된다");
        CHECK(strcmp(last_line(), "Sm!") == 0,
              "B1 게이트웨이 전문의 **마지막 줄**이 Sm! 다 — 델파이 Lines[Count-2] 계약");
    }
    // ── B2 이음매 길이 훑기 — 어디까지 살아나는가 ──
    uint32_t d12 = 0, d18 = 0, d22 = 0, d30 = 0;
    {
        const bool r12 = seam_case(1200, 0x72, "BB0012", &d12);
        const bool r18 = seam_case(1800, 0x73, "BB0018", &d18);
        const bool r22 = seam_case(2200, 0x74, "BB0022", &d22);
        const bool r30 = seam_case(3000, 0x75, "BB0030", &d30);
        tlog("  B2 이음매 1.2s=%d(%lums) 1.8s=%d(%lums) 2.2s=%d(%lums) 3.0s=%d(%lums)\n",
             r12, (unsigned long)d12, r18, (unsigned long)d18,
             r22, (unsigned long)d22, r30, (unsigned long)d30);
        CHECK(r12 && r18, "B2 1초를 넘는 이음매(1.2·1.8초)는 이어 붙여 살아난다 — v2.2.23 의 목적");
        // [9차 판정] 창을 2초에서 더 늘리지 않는다 — 대기를 늘리면 그만큼 카드 폴링이 멈춘다(접촉 1초 미만).
        //  늘리기로 결정하면 이 줄이 걸리게 해 두는 것이 목적이다.
        CHECK(!r22 && !r30,
              "B2 판정: 2.2·3.0초 이음매는 살리지 않는다(첫 빈 읽기에서 break — 창은 약 2초)");
    }
    // ── B3 꼬리가 영영 안 오고, 그 대기 중에 PC 의 레거시 시각 동기 'T' 가 도착한다 ──
    uint32_t dNever = 0;
    {
        sim_advance_ms(60UL * 1000);
        drain();
        const DateTime before = rtc.GetCurrentDateTime();
        const uint32_t t0 = g_ms + 10;
        const char c1[] = "G10000;G22026;9;27;0;12;00;0;G3CCC003;NAMEC;;G4SUBJC;;;";
        const char c2[] = "T2026;9;27;0;18;30;0;";      // 세척관리 rfid_serial.py:1499-1503 형식
        serial_queue(c1, sizeof(c1) - 1, t0);
        serial_queue(c2, sizeof(c2) - 1, t0 + 1500);
        sim_advance_ms(20);
        logs_clear();
        const uint32_t t = g_ms;
        GUARDED(serialEvent());
        dNever = g_ms - t;
        // ★"아직 안 읽었다" 와 "먹혀서 사라졌다" 를 가른다 — 남은 입력을 끝까지 읽혀 본다.
        //  이어 붙이기가 없으면 T 는 링에 남아 다음 serialEvent 가 처리한다(m3 음성대조).
        const uint16_t left = (uint16_t)Serial.available();
        for (uint8_t k = 0; k < 4 && Serial.available() > 0; ++k) GUARDED(serialEvent());
        run_loops(1);
        const DateTime after = rtc.GetCurrentDateTime();
        tlog("  B3 꼬리 없음 + 대기 중 T: serialEvent %lu ms · 수신음(500)=%u · 남은바이트=%u · 시각 %02u:%02u → %02u:%02u · updated=%d\n",
             (unsigned long)dNever, buzz_count(500), left, before.hour(), before.minute(),
             after.hour(), after.minute(), lcd_has("updated"));
        // [9차 판정] 대기 중 도착한 비-게이트웨이 명령은 유실된다 — 되살리려면 조각을 따로 보관하는 상태가
        //  필요하고(상태를 늘리는 봉합), 조합이 드물다. 사람이 다시 누르면 된다. 고치기로 하면 이 줄이 걸린다.
        CHECK(left == 0 && after.hour() != 18 && !lcd_has("updated"),
              "B3 판정: 이어 붙이기 대기 중에 온 'T' 는 유실된다(링에도 남지 않는다)");
        CHECK(buzz_count(500) == 1, "B3 그래도 수신 확인음은 난다 — 사람도 PC 도 유실을 모른다");
        CHECK(dNever > dOne + 1800,
              "B3 꼬리 없는 버퍼는 온전한 버퍼보다 1초 대기 두 번만큼 더 붙잡는다");
        // 대조: 같은 T 를 단독으로 보내면 시계가 맞춰진다(시험이 이 행위를 실제로 본다는 증거)
        drain();
        logs_clear();
        serial_inject("T2026;9;27;0;18;30;0;", 21);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime ctl = rtc.GetCurrentDateTime();
        tlog("  B3 대조 단독 T → %02u:%02u updated=%d\n", ctl.hour(), ctl.minute(), lcd_has("updated"));
        CHECK(ctl.hour() == 18 && ctl.minute() == 30, "B3 대조: 단독 'T' 는 게이트웨이에서도 시계를 맞춘다");
    }
    // ── B4 같은 대기 중에 설정 JSON(cfg_get_config)이 도착한다 → 응답이 없다 ──
    {
        sim_advance_ms(60UL * 1000);
        drain();
        const uint32_t t0 = g_ms + 10;
        const char c1[] = "G10000;G22026;9;27;0;13;00;0;G3DDD004;NAMED;;G4SUBJD;;;";
        const char c2[] = "\x02{\"cmd\":\"cfg_get_config\"}\x03";
        serial_queue(c1, sizeof(c1) - 1, t0);
        serial_queue(c2, sizeof(c2) - 1, t0 + 1500);
        sim_advance_ms(20);
        logs_clear();
        GUARDED(serialEvent());
        const bool resp = serial_has("device_type") || serial_has("washing_time");
        tlog("  B4 대기 중 cfg_get_config → 응답=%d\n", resp);
        CHECK(!resp, "B4 판정: 대기 중에 온 설정 JSON 도 유실된다(B3 과 같은 자리)");
        drain();
        logs_clear();
        serial_inject("\x02{\"cmd\":\"cfg_get_config\"}\x03", 26);
        GUARDED(serialEvent());
        run_loops(1);
        const bool resp2 = serial_has("device_type") || serial_has("washing_time");
        tlog("  B4 대조 단독 JSON → 응답=%d\n", resp2);
        CHECK(resp2, "B4 대조: 단독 cfg_get_config 는 정상 응답한다");
    }
    tlog("  요약 blocking(ms): 온전 %lu · 이음매1.2 %lu · 1.8 %lu · 2.2 %lu · 3.0 %lu · 꼬리없음 %lu · resets=%u\n",
         (unsigned long)dOne, (unsigned long)d12, (unsigned long)d18, (unsigned long)d22,
         (unsigned long)d30, (unsigned long)dNever, g_resetCount);
    done();
    for (;;) {}
}
