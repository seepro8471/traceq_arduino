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
    // ── [18차] type 2(공장 복원) — 그동안 시험이 없던 경로. 데이터가 지워지고 16섹터 전부 공장 트레일러(KEY_B 인증 불가)로 ──
    {
        static SimCard tq2;
        make_tag(tq2, 0x45, SCOPE_TYPE_TAG, 45, "SC0045", "S0045");
        card_place(&tq2);
        logs_clear();
        json("{\"cmd\":\"cfg_new_tag\",\"type_id\":2}");
        bool allFactory = true;
        for (uint8_t s = 0; s < 16; ++s)
            if (tq2.keyBAuth[s] || tq2.keyB[s][0] != 0xFF || tq2.data[s * 4 + 3][6] != 0xFF || tq2.data[s * 4 + 3][7] != 0x07) allFactory = false;
        Company c2{};
        memcpy(&c2, tq2.data[SECTOR0_COMPANY], sizeof(c2));
        tlog("  type2: lcd=[%.40s] 공장트레일러=%d company=%lu\n", g_lcdLog, allFactory, (unsigned long)c2.CompanyCode);
        CHECK(lcd_has("tag created") && allFactory && c2.CompanyCode == 0 && tq2.data[SECTOR0_TAG][0] == 0,
              "18차: type 2 → 데이터 소거 + 16섹터 공장 트레일러(tag created)");
        card_remove();
        run_loops(4);

        // 공장 복원이 도중에 끊긴 태그(앞 6섹터만 공장 키) — [18차 판정] type 2 재시도는 실패하고(사실), type 0 뒤 type 2 로 회복한다
        make_tag(tq2, 0x46, SCOPE_TYPE_TAG, 46, "SC0046", "S0046");
        for (uint8_t s = 0; s < 6; ++s)
        {
            memset(tq2.keyB[s], 0xFF, 6); tq2.keyBAuth[s] = false;
            tq2.data[s * 4 + 3][6] = 0xFF; tq2.data[s * 4 + 3][7] = 0x07; tq2.data[s * 4 + 3][8] = 0x80;
        }
        card_place(&tq2);
        logs_clear(); buzz_clear();
        json("{\"cmd\":\"cfg_new_tag\",\"type_id\":2}");
        const bool retryFails = lcd_has("timeout or error") && !lcd_has("tag created") && buzz_count(100) == 4;
        card_remove(); run_loops(4);
        card_place(&tq2);
        logs_clear();
        json("{\"cmd\":\"cfg_new_tag\",\"type_id\":0}");      // 신규 태그(키 설치는 멱등 — 이미 TraceQ 인 섹터는 건너뛴다)
        bool allTraceQ = true;
        for (uint8_t s = 0; s < 16; ++s) if (!tq2.keyBAuth[s]) allTraceQ = false;
        const bool rekeyed = lcd_has("tag created") && allTraceQ;
        card_remove(); run_loops(4);
        card_place(&tq2);
        logs_clear();
        json("{\"cmd\":\"cfg_new_tag\",\"type_id\":2}");
        allFactory = true;
        for (uint8_t s = 0; s < 16; ++s)
            if (tq2.keyBAuth[s] || tq2.keyB[s][0] != 0xFF || tq2.data[s * 4 + 3][7] != 0x07) allFactory = false;
        tlog("  반쯤 복원된 태그: type2 재시도 실패=%d → type0 재설치=%d → type2 lcd=[%.40s] 공장트레일러=%d\n", retryFails, rekeyed, g_lcdLog, allFactory);
        CHECK(retryFails, "18차 사실: 공장 복원이 끊긴 태그에 type 2 를 다시 하면 실패음 `timeout or error`(첫 섹터 인증에서 막힌다)");
        CHECK(rekeyed && lcd_has("tag created") && allFactory && tq2.data[SECTOR0_TAG][0] == 0,
              "18차 판정의 회복 절차: type 0(신규 태그) 뒤 type 2 → 16섹터 전부 공장 복원");
        card_remove();
        run_loops(4);
    }
    // ── [18차] type_id 관문 — 칸이 없거나 0~2 밖이면 어떤 태그도 건드리지 않고 곧바로 실패로 알린다 ──
    //  종전: 누락 = 0(키 설치·소거) · 모르는 값 = 1(소거)로 돌아 올려 둔 태그의 기록이 지워졌다.
    {
        static SimCard keep;
        const char *bad[] = {"{\"cmd\":\"cfg_new_tag\"}", "{\"cmd\":\"cfg_new_tag\",\"type_id\":7}", "{\"cmd\":\"cfg_new_tag\",\"type_id\":-1}"};
        bool allKept = true;
        for (uint8_t i = 0; i < 3; ++i)
        {
            make_tag(keep, 0x47 + i, SCOPE_TYPE_TAG, 47, "SC0047", "S0047");
            set_process(keep, Process{1, 0, 1, 0, 1, false, 0, 1});
            card_place(&keep);
            logs_clear(); buzz_clear();
            const uint32_t t0 = g_ms;
            const uint16_t w0 = keep.writeCount;
            json(bad[i]);
            Company ck{};
            memcpy(&ck, keep.data[SECTOR0_COMPANY], sizeof(ck));
            const bool kept = lcd_has("timeout or error") && !lcd_has("tag created") && buzz_count(100) == 4 && keep.writeCount == w0 &&
                              ck.CompanyCode == TRACEQ_COMPANY_CODE && get_process(keep).WashingStatus == 1 && (g_ms - t0) < 2000UL;
            tlog("  type_id 관문[%u]: 실패알림=%d 쓰기=%u 회사코드유지=%d 걸린 %lums → %d\n", i, lcd_has("timeout or error"),
                 (unsigned)(keep.writeCount - w0), ck.CompanyCode == TRACEQ_COMPANY_CODE, (unsigned long)(g_ms - t0), kept);
            if (!kept) allKept = false;
            card_remove();
            run_loops(4);
        }
        CHECK(allKept, "18차: type_id 누락·7·-1 → 태그에 쓰기 0 · 실패음 100×4 `timeout or error` · 4초를 기다리지 않는다");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
