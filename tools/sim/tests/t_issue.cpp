// 태그 발급(cfg_new_tag) — 설정 프로그램 안내대로 "먼저 올려 두고 버튼" 순서가 동작하는가.
#include "common.h"

static SimCard fac, fac2;

static void factory(SimCard &c, uint8_t uid)
{
    card_init_traceq(c, uid);
    for (uint8_t s = 0; s < 16; ++s)
    {
        memset(c.keyB[s], 0xFF, 6); c.keyBAuth[s] = false;   // 공장 트레일러(운송 접근조건 FF 07 80)
        c.data[s * 4 + 3][6] = 0xFF; c.data[s * 4 + 3][7] = 0x07; c.data[s * 4 + 3][8] = 0x80;
    }
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

    // ── [3차 A] 운영 루프가 이미 처리해 정지(HALT)시킨 TraceQ 태그를 올려 둔 채 type 1(데이터 초기화) ──
    //    REQA 만으로는 정지 카드를 못 깨워 항상 timeout — 발급 대기는 WUPA 로 깨운다.
    {
        static SimCard mgr, tq;
        make_tag(mgr, 0x44, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
        touch(mgr);                                   // 세척 시작이 거부되지 않게 담당자 등록
        make_tag(tq, 0x43, SCOPE_TYPE_TAG, 43, "SC0043", "S0043");
        set_process(tq, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_place(&tq);
        run_loops(8);                                 // 세척기가 세척 시작 처리 → HaltA
        CHECK(get_process(tq).Rewrite == 1, "3차A: 루프가 먼저 처리해 정지 상태");
        logs_clear();
        json("{\"cmd\":\"cfg_new_tag\",\"type_id\":1}");
        const bool cleared = get_process(tq).Rewrite == 0 && tq.data[SECTOR0_TAG][0] == 0;
        tlog("  정지 태그 type1: lcd=[%.60s] RW=%u\n", g_lcdLog, get_process(tq).Rewrite);
        CHECK(lcd_has("tag created") && cleared, "3차A: 정지된 TraceQ 태그도 발급 명령이 잡는다(WUPA)");
        card_remove();
        run_loops(4);
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
