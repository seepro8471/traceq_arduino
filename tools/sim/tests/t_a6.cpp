// 4차 P3 마무리(09-26) 잠금 — 쓰이지 않는 읽기가 소독 시작을 깨지 않는다(C-3) · 게터가 세터 범위 밖
// 값을 돌려주지 않는다(C-4) · 알람 남은시간은 절대 시각 차(G P2-3) · 폐기 블록 소거 실패는 0 덤프(H P3-H4).
#include "common.h"

static SimCard mgr, a, b;

static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); }
static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));

    // ── C-4 게터는 세터가 저장하지 않는 값을 돌려주지 않는다 ──
    //    손상·구판 바이트를 EEPROM 에 직접 박아 놓고 게터를 본다(세터를 안 거친다).
    {
        boot('W');
        EEPROM.put(97, static_cast<int8_t>(-30));       // 세척시간(AlarmOption mTimeSlot1Addr)
        EEPROM.put(98, static_cast<int8_t>(-1));        // 소독시간
        EEPROM.put(166, static_cast<uint8_t>(9));       // 동시소독 슬롯(세터는 0~2)
        EEPROM.put(165, static_cast<uint8_t>(250));     // 동시소독 지연(세터는 0~120)
        tlog("  C-4 범위 밖 박음: 세척=%d 소독=%d 슬롯=%u 지연=%u\n", alarmOption.GetTimeSlot1(),
             alarmOption.GetTimeSlot2(), disinfectionOption.GetSimultaneousDisinfectionSlot(),
             disinfectionOption.GetSimultaneousDisinfectionDelay());
        CHECK(alarmOption.GetTimeSlot1() == 0 && alarmOption.GetTimeSlot2() == 0,
              "C-4 음수 시간은 0 으로 — 자동 종료가 시작보다 앞서지 않게");
        CHECK(disinfectionOption.GetSimultaneousDisinfectionSlot() == 2 &&
              disinfectionOption.GetSimultaneousDisinfectionDelay() == 120,
              "C-4 동시소독 슬롯·지연 게터는 세터 상한(2·120)까지만");
        alarmOption.SetTimeSlot1(4);
        alarmOption.SetTimeSlot2(18);
        disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    }

    // ── C-3 쓰이지 않는 detail 읽기가 소독 시작을 무너뜨리지 않는다 ──
    //    SECTOR14 읽기만 실패시킨다. 그 앞에서 옛 기록(섹터 5·6)을 이미 지웠으므로,
    //    시작이 취소되면 "옛 기록 없음 + 새 시작 없음" 태그가 남는다.
    reboot_as('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND014567890123", "KIMJH");
    touch(mgr);
    {
        washed_scope(a, 0x21, 21);
        // ★첫 읽기는 통과시키고 그 뒤를 전부 실패 — 읽기가 남아 있으면 '쓰이지 않는 읽기' 가 첫 읽기를
        //  먹고 쓰기 검증 읽기가 실패해 시작이 깨진다. 읽기를 없앤 지금은 검증 읽기가 첫 읽기라 통과한다.
        a.readErrBlock = SECTOR14_DISINFECTION_DETAIL;
        a.readErrSkip  = 1;
        a.readErrTimes = 10;
        logs_clear(); buzz_clear();
        touch(a);
        const Process p = get_process(a);
        const LocalDateTime st = get_ldt(a, SECTOR5_DISINFECTION_START);
        tlog("  C-3 detail 읽기 실패: RW=%u 시작시각 년=%u 성공음=%u\n", p.Rewrite, st.Date.Year, buzz_count(50));
        CHECK(p.Rewrite == 2 && st.Date.Year != 0 && buzz_count(50) == 1,
              "C-3 쓰이지 않는 detail 읽기가 실패해도 소독 시작은 정상 기록된다");
    }

    // ── G P2-3 알람 남은 시간: 60분 초과 설정에서 나머지만 보이지 않는다 ──
    {
        alarmOption.SetTimeSlot2(90);                   // 90분
        rtc_set(rel_date(10, 0, 0));
        washed_scope(b, 0x22, 22);
        touch(b);                                       // 소독 시작 → 알람 90분
        ui.InvalidateHome();                            // 1행 캐시 무효화 — 같은 내용이라도 다시 그리게
        logs_clear();
        run_loops(2);
        tlog("  G2-3 90분 알람 화면: [%.40s]\n", g_lcdLog);
        CHECK(lcd_has("89:") || lcd_has("90:"), "G P2-3 90분 알람의 남은 시간이 89~90분으로 보인다(나머지 29분 아님)");

        // 자정을 넘는 알람 — 종전엔 하루 안 시각끼리 빼서 음수가 됐다.
        rtc_set(DateTime(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY, 23, 30, 0));
        washed_scope(a, 0x24, 24);
        touch(a);                                       // 23:30 + 90분 = 다음날 01:00
        ui.InvalidateHome();
        logs_clear();
        run_loops(2);
        tlog("  G2-3 자정 넘김 화면: [%.44s]\n", g_lcdLog);
        CHECK(lcd_has("89:") || lcd_has("90:"),
              "G P2-3 자정을 넘는 알람도 남은 시간이 89~90분(음수·쓰레기 아님)");
        rtc_set(rel_date(10, 0, 0));
        alarmOption.SetTimeSlot2(18);
    }

    // ── H P3-H4 폐기 블록(18) 소거가 실패하면 옛 바이트를 보내지 않는다 ──
    reboot_as('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        washed_scope(a, 0x23, 23);
        set_process(a, Process{1, 1, 1, 1, 1, false, 0, 2});
        set_record(a, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
        set_record(a, SECTOR6_DISINFECTION_END,   2, rel_date(9, 23, 0));
        memcpy(a.data[SECTOR4_DISINFECTION], "STALE1", 7);   // 옛 바이트
        a.nackBlock = SECTOR4_DISINFECTION;                  // 그 블록 소거만 실패(카드는 산다)
        logs_clear();
        serial_inject("Z", 1);
        touch(a);
        // 덤프 줄은 "1312" + 16바이트 hex. 옛 값 'S'(0x53) 가 나가면 안 되고 전부 0 이어야 한다.
        const bool zeroLine = serial_has("131200000000000000000000000000000000");
        tlog("  H4 소거 실패: 0 줄=%d 옛값(53)=%d Ok!=%d\n", zeroLine, serial_has("13125354414C4531"), serial_has("Ok!"));
        CHECK(zeroLine && !serial_has("13125354414C4531"),
              "H P3-H4 폐기 블록 소거 실패 → 옛 바이트 대신 0 을 보낸다(와이어 계약)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
