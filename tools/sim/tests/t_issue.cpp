// 태그 발급(cfg_new_tag) — 설정 프로그램 안내대로 "먼저 올려 두고 버튼" 순서가 동작하는가.
#include "common.h"

static SimCard fac, fac2;

static void factory(SimCard &c, uint8_t uid)
{
    card_init_traceq(c, uid);
    for (uint8_t s = 0; s < 16; ++s) { memset(c.keyB[s], 0xFF, 6); c.keyBAuth[s] = false; }   // 공장 트레일러
}
static void json(const char *s)
{
    serial_inject(s, strlen(s));
    GUARDED(serialEvent());
}

int main()
{
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));
    boot('W');

    // ── 공장 태그를 먼저 올려 둔 뒤 type 0 발급 ──
    factory(fac, 0x41);
    card_place(&fac);
    run_loops(8);                                     // 루프가 먼저 한 번 처리(AuthFailed)
    logs_clear();
    json("{\"cmd\":\"cfg_new_tag\",\"type_id\":0}");
    Company co{};
    memcpy(&co, fac.data[SECTOR0_COMPANY], sizeof(co));
    tlog("  먼저 올려 둠: lcd=[%.80s] company=%lu keyBAuth0=%d\n", g_lcdLog, (unsigned long)co.CompanyCode, fac.keyBAuth[0]);
    CHECK(lcd_has("tag created"), "발급: 먼저 올려 둔 공장 태그 → tag created");
    CHECK(co.CompanyCode == TRACEQ_COMPANY_CODE && fac.keyBAuth[15], "발급: 회사코드·TraceQ 키 설치");
    card_remove();
    run_loops(4);

    // ── [회귀] 명령 뒤에 태그를 대는 순서 ──
    factory(fac2, 0x42);
    logs_clear();
    const char *cmd = "{\"cmd\":\"cfg_new_tag\",\"type_id\":0}";
    serial_inject(cmd, strlen(cmd));
    card_place(&fac2);                                // 명령 대기 루프 안에서 카드가 들어온다
    GUARDED(serialEvent());
    memcpy(&co, fac2.data[SECTOR0_COMPANY], sizeof(co));
    CHECK(lcd_has("tag created") && co.CompanyCode == TRACEQ_COMPANY_CODE, "회귀: 명령 후 태그 → tag created");
    card_remove();
    run_loops(4);
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
