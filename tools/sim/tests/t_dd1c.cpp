// DD1c — 8회차가 새로 만든 두 행위의 **구별 표본**(지금 저장소 22종으로는 되돌려도 빨강이 안 난다).
//  §6 IsGatewayFrame 의 "'{' 와 G 마커 중 먼저 오는 쪽" — 값에 ';G2' 가 든 설정 JSON 은 JSON 으로 가야 한다.
//  §7 꼬리 표지를 **이어 붙인 뒤** 길이로 정한다 — 붙여서 511 을 채우면 다음 버퍼는 그 전문의 꼬리다.
#include "common.h"

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

int main()
{
    rtc_set(rel_date(19, 0, 0));
    boot('G');

    // ── §6 게이트웨이에 온 설정 JSON 의 값 안에 ';G2' 가 있다(BB1 P3-5 자리) ──
    {
        char js[160];
        snprintf(js, sizeof(js),
                 "{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"%04u-%02u-%02u 20:15:00\","
                 "\"note\":\";G2\"}",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        logs_clear();
        serial_inject(js, strlen(js));
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  (6) ';G2' 든 설정 JSON 뒤 %02u:%02u (updated=%d)\n", now.hour(), now.minute(),
             lcd_has("updated"));
        CHECK(now.hour() == 20 && now.minute() == 15,
              "(6) 값에 ';G2' 가 든 설정 JSON 은 게이트웨이에서도 JSON 으로 처리된다('{' 가 앞이다)");
    }

    // ── §7 이어 붙여 511 을 채운 뒤에 오는 버퍼는 꼬리다(그 안의 'T' 를 실행하면 안 된다) ──
    {
        rtc_set(rel_date(19, 0, 0));
        sim_advance_ms(60UL * 1000);
        logs_clear();
        char c1[301];
        const char headPart[] = "G10000;G22026;9;23;3;19;30;0;G3PT0007;NAMEH;;G4";
        memset(c1, 'X', sizeof(c1));
        memcpy(c1, headPart, sizeof(headPart) - 1);       // 꼬리(G5) 없음 · 나머지는 'X' 300바이트
        char c2[240];
        memset(c2, 'Y', 211);                             // 앞 211 이 511 을 채운다
        char tcmd[32];
        snprintf(tcmd, sizeof(tcmd), "T%u;%u;%u;3;23;59;59;", (unsigned)TRACEQ_RELEASE_YEAR,
                 (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        const size_t tlen = strlen(tcmd);
        memcpy(c2 + 211, tcmd, tlen);
        const uint32_t t0 = g_ms + 50;
        serial_queue(c1, 300, t0);
        serial_queue(c2, 211 + tlen, t0 + 1800);
        pump(9000);
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  (7) 이어 붙여 511 뒤 꼬리 'T' → %02u:%02u (19:00 유지해야 한다 · updated=%d)\n",
             now.hour(), now.minute(), lcd_has("updated"));
        CHECK(now.hour() == 19,
              "(7) 이어 붙여 511 을 채운 다음 버퍼의 'T' 는 실행되지 않는다(꼬리 표지)");
    }

    done();
    for (;;) {}
}
