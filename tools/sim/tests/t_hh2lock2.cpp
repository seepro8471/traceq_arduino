// HH2 ⑤ 두 번째 잠금 — `t_notify` 의 warned() 가 닿지 않던 실패음 자리 중
// 내 측정 시험이 아직 안 보는 넷. HEAD 초록 · 변이 u3 에서 빨강.
#include "hh2_sound.h"

static SimCard mgr, a;

static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); }
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── N1 세척 종료 터치의 더블터치 판정 읽기 실패 = 실패음 (WashingProcessor.cpp:21) ──
    fresh_scope(a, 0x11, 11);
    set_process(a, Process{1, 0, 1, 0, 1, false, 0, 1});      // Rewrite=1 → 종료 갈래
    set_record(a, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    a.readErrBlock = SECTOR2_WASHING_START; a.readErrTimes = 8;
    snd_begin(); touch(a);
    tlog("  N1 세척 판정 읽기 실패: 100=%u 50=%u lcd=%d\n",
         buzz_count(100), buzz_count(50), lcd_has("Read Error"));
    CHECK(lcd_has("Read Error") && buzz_count(100) == 4 && buzz_count(50) == 0,
          "N1 세척 더블터치 판정 읽기 실패 = 실패음(WashingProcessor.cpp:21)");
    a.readErrBlock = -1; a.readErrTimes = 0;

    // ── N2 공정 블록 읽기 실패 = 실패음 (RecordProcessor.cpp:43) ──
    fresh_scope(a, 0x12, 12);
    a.readErrBlock = SECTOR1_PROCESS; a.readErrTimes = 8;
    snd_begin(); touch(a);
    tlog("  N2 공정 읽기 실패: 100=%u 50=%u lcd=%d\n",
         buzz_count(100), buzz_count(50), lcd_has("Read Error"));
    CHECK(lcd_has("Read Error") && buzz_count(100) == 4 && buzz_count(50) == 0,
          "N2 공정 블록 읽기 실패 = 실패음(RecordProcessor.cpp:43)");
    a.readErrBlock = -1; a.readErrTimes = 0;

    // ── N3 소독 종료 터치의 더블터치 판정 읽기 실패 = 실패음 (DisinfectionProcessor.cpp:48) ──
    reboot_as('D');
    touch(mgr);
    fresh_scope(a, 0x13, 13);
    set_process(a, Process{1, 1, 1, 1, 1, false, 0, 2});      // Rewrite=2 → 종료 갈래
    set_record(a, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(a, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    a.readErrBlock = SECTOR5_DISINFECTION_START; a.readErrTimes = 8;
    snd_begin(); touch(a);
    tlog("  N3 소독 판정 읽기 실패: 100=%u 50=%u lcd=%d\n",
         buzz_count(100), buzz_count(50), lcd_has("Read Error"));
    CHECK(lcd_has("Read Error") && buzz_count(100) == 4 && buzz_count(50) == 0,
          "N3 소독 더블터치 판정 읽기 실패 = 실패음(DisinfectionProcessor.cpp:48)");
    a.readErrBlock = -1; a.readErrTimes = 0;

    // ── N4 설정기 발급(cfg_new_tag) 대기 만료 = 실패음 (SerialProcessor.cpp:148) ──
    reboot_as('S');
    snd_begin();
    serial_inject("{\"cmd\":\"cfg_new_tag\",\"type_id\":1}", 34);
    GUARDED(serialEvent());
    tlog("  N4 cfg_new_tag 만료: 100=%u 500=%u lcd=%d\n",
         buzz_count(100), buzz_count(500), lcd_has("timeout or error"));
    CHECK(lcd_has("timeout or error") && buzz_count(100) == 4 && buzz_count(500) == 0,
          "N4 설정기 발급 대기 만료 = 실패음(SerialProcessor.cpp:148)");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
