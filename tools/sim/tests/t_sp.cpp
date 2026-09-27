// AA3 — 스택 최악 조합. 511바이트 수신과 그 안에서 벌어지는 가장 깊은 일들을 일부러 겹친다.
// 판정은 g_spDepthMax(한 번의 loop()/serialEvent() 호출이 쓴 바이트, 가짜 하드웨어 표본 기준).
#include "common.h"

static SimCard sc, sc2;

static void done_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 22, 9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, DateTime(2026, 9, 22, 9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, DateTime(2026, 9, 22, 9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 2, DateTime(2026, 9, 22, 9, 23, 0));
}
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}

// 511바이트로 채운 전문 — 앞에 head 를 놓고 뒤를 pad 로 메운다(readBytes 한도와 같은 길이).
static char s_buf[512];
static size_t fill511(const char *head, char pad)
{
    const size_t n = strlen(head);
    memset(s_buf, pad, 511);
    memcpy(s_buf, head, n < 511 ? n : 511);
    return 511;
}
static uint16_t s_depth[9];    // 가짜 하드웨어 표본 기준(호출별)
static uint16_t s_paint[9];    // 무늬 기준(가짜를 안 부르는 구간도 포함)
static void mark(uint8_t i)
{
    s_depth[i] = g_spDepthMax; g_spDepthMax = 0;
    const uint16_t low = sim_paint_low();
    const uint16_t d = (g_spCallBase > low) ? (uint16_t)(g_spCallBase - low) : 0;
    if (d > s_paint[i]) s_paint[i] = d;
    sim_paint();
}
// 511 을 읽은 다음 호출은 "끊긴 전문의 꼬리" 로 버려진다(제품 규칙) — 짧은 한 번으로 그 표지를 내린다.
static void clear_tail()
{
    serial_inject("q", 1);
    GUARDED(serialEvent());
    g_spDepthMax = 0;
    sim_paint();
}
static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); clear_tail(); }

int main()
{
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));
    boot('S');
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    g_spDepthMax = 0;
    sim_paint();

    // ① 511바이트 cfg_get_config — serialEvent(버퍼 512) + WriteOptionData(StaticJsonDocument 512) + serializeJson
    {
        logs_clear();
        const size_t n = fill511("{\"cmd\":\"cfg_get_config\"} ", ' ');
        serial_inject(s_buf, n);
        GUARDED(serialEvent());
        mark(1);
        CHECK(serial_has("device_type") && serial_has("df_clear_date_time"),
              "①511B cfg_get_config: 설정 전문이 나갔다");
    }
    // ② 511바이트 S 발급 명령 + 리더 위 태그 — serialEvent 안에서 카드 쓰기(가장 깊은 MFRC522 사슬)
    {
        sim_advance_ms(60UL * 1000);
        clear_tail();
        fresh_scope(sc, 0x41, 41);
        card_place(&sc);
        run_loops(2);
        logs_clear();
        const size_t n = fill511("S77;SER77;", 'X');
        serial_inject(s_buf, n);
        GUARDED(serialEvent());
        mark(2);
        Tag t{};
        memcpy(&t, sc.data[SECTOR0_TAG], sizeof(t));
        CHECK(lcd_has("new tag") && t.Number == 77, "②511B S 발급 + 카드 쓰기: 태그가 발급됐다");
        card_remove();
        run_loops(2);
    }
    // ③ 511바이트 cfg_set_config — JSON 파싱 + EEPROM 기록
    {
        sim_advance_ms(60UL * 1000);
        clear_tail();
        logs_clear();
        const size_t n = fill511("{\"cmd\":\"cfg_set_config\",\"washing_time\":33,\"df_max_cnt\":44} ", ' ');
        serial_inject(s_buf, n);
        GUARDED(serialEvent());
        mark(3);
        CHECK(alarmOption.GetTimeSlot1() == 33 && disinfectionOption.GetMaximumCount() == 44,
              "③511B cfg_set_config: 설정이 저장됐다");
    }
    // ④ 511바이트 안의 깊은 중첩 JSON — parseVariant 재귀(NestingLimit)
    {
        sim_advance_ms(60UL * 1000);
        clear_tail();
        logs_clear();
        char deep[512];
        size_t k = 0;
        deep[k++] = '{';
        for (uint8_t i = 0; i < 40; ++i) { memcpy(deep + k, "\"a\":{", 5); k += 5; }
        memcpy(deep + k, "\"a\":1}", 6); k += 6;
        while (k < 511) deep[k++] = ' ';
        serial_inject(deep, 511);
        GUARDED(serialEvent());
        mark(4);
        CHECK(g_lcdLog[0] != 0, "④중첩 40단 JSON: 안내가 떴다(파싱 실패 경로)");
    }
    // ⑤ 게이트웨이 511바이트 병합 전문 + 리더 위 태그 — GatewaySerialEvent + write_patient_info
    {
        reboot_as('G');
        logs_clear();
        // G2 는 년;월;일;요일;시;분;초; 일곱 칸(요일 칸이 빠지면 시가 30 이 되어 범위 밖으로 버려진다)
        const size_t n = fill511("G10000;G22026;9;22;4;10;30;0;G312345;HONG;;G4EGD;;;G51995;04;19;F;", ' ');
        serial_inject(s_buf, n);
        GUARDED(serialEvent());
        mark(5);
        fresh_scope(sc2, 0x52, 52);
        card_place(&sc2);
        sim_paint();
        run_loops(3);
        const uint16_t d5 = g_spDepthMax;
        { const uint16_t low = sim_paint_low();
          const uint16_t d = (g_spCallBase > low) ? (uint16_t)(g_spCallBase - low) : 0;
          if (d > s_paint[5]) s_paint[5] = d; }
        card_remove();
        run_loops(2);
        const LocalDateTime d = get_ldt(sc2, SECTOR1_GATEWAY);
        tlog("  ⑤게이트웨이 loop(태그 기록) 깊이=%u\n", d5);
        CHECK(ldt_eq(d, 2026, 9, 22, 10, 30, 0), "⑤511B 게이트웨이 병합 → 태그 검사일시 기록");
        if (d5 > s_depth[5]) s_depth[5] = d5;
        g_spDepthMax = 0;
    }
    // ⑥ 소독기 메뉴 깊은 편집 — loop 갈래(serialEvent 와 겹치지 않는다: 코어가 loop 뒤에 부른다)
    {
        reboot_as('D');
        g_btnIdleLimit = 1000000UL;
        g_spDepthMax = 0;
        buttons_script("S" "RS" "RRSS");     // 메뉴 진입 → 항목 이동 → 편집
        GUARDED(loop());
        mark(6);
        CHECK(s_depth[6] > 0, "⑥메뉴 편집 경로가 실제로 실행됐다");
    }
    // ⑦ 서버 전체 덤프(블록 60여 개) — loop 갈래
    {
        reboot_as('S');
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        done_scope(sc, 0x61, 61);
        logs_clear();
        serial_inject("Z", 1);
        g_spDepthMax = 0;
        touch(sc);
        mark(7);
        CHECK(serial_has("Ok!"), "⑦전체 덤프가 끝났다");
    }
    // ⑧ 덤프 직후 같은 회차에 511바이트 수신 — 코어 순서(loop → serialEvent)를 그대로
    {
        sim_advance_ms(60UL * 1000);
        clear_tail();
        done_scope(sc, 0x62, 62);
        serial_inject("Z", 1);
        card_place(&sc);
        g_spDepthMax = 0;
        GUARDED(loop());
        const size_t n = fill511("{\"cmd\":\"cfg_get_config\"} ", ' ');
        serial_inject(s_buf, n);
        GUARDED(serialEvent());
        mark(8);
        card_remove();
        run_loops(2);
        CHECK(s_depth[8] > 0, "⑧덤프+511B 수신이 같은 회차에 실행됐다");
    }

    for (uint8_t i = 1; i <= 8; ++i) tlog("  DEPTH%u=%u PAINT%u=%u\n", i, s_depth[i], i, s_paint[i]);
    uint16_t worst = 0;
    for (uint8_t i = 1; i <= 8; ++i) if (s_depth[i] > worst) worst = s_depth[i];
    for (uint8_t i = 1; i <= 8; ++i) if (s_paint[i] > worst) worst = s_paint[i];
    tlog("  WORST=%u\n", worst);
    // 관문: 한 번의 loop()/serialEvent() 가 2000B 를 넘게 쓰면 실칩(자유 5163B)에서 여유가 3KB 아래로 떨어진다.
    CHECK(worst < 2000, "최악 조합에서도 한 호출의 스택이 2000B 미만");
    done();
    for (;;) {}
}
