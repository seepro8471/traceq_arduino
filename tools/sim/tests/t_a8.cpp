// 5차 재검증(V1~V4 · 판정 2 · W1·W2) 잠금 — 각 수정을 되돌리면 여기서 빨강이 나야 한다. (t_a7 에서 분리 — 시뮬 RAM 관문)
#include "common.h"

static SimCard mgr, a, b, c;

static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); }
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}
static void json(const char *s) { serial_inject(s, strlen(s)); GUARDED(serialEvent()); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND014567890123", "KIMJH");
    touch(mgr);

    // ═══ 재검증(V1·V2·V3) 잠금 ═══
    // V1: 기기번호는 15열부터 5칸 — 3자리→2자리로 줄어도 잔상 없음 · "Scope : 32767"
    reboot_as('W');
    {
        deviceOption.SetNumber(120);
        ui.InvalidateHome(); logs_clear(); run_loops(1);
        CHECK(lcd_has("W:120"), "V1 기기번호 세 자리 표시");
        deviceOption.SetNumber(5);
        ui.InvalidateHome(); logs_clear(); run_loops(1);
        tlog("  V1 번호 120→5 뒤 3행: [%.24s]\n", g_lcdLog);
        CHECK(lcd_has(" W:05") && !lcd_has("WW:05"), "V1 기기번호 5칸 고정 — 자릿수가 줄어도 잔상(WW:05) 없음");
        deviceOption.SetNumber(5000);
        CHECK(deviceOption.GetNumber() == 999, "V1 기기번호 상한 999(5칸)");
        deviceOption.SetNumber(1);
    }
    reboot_as('G');
    {
        char pk[96];
        snprintf(pk, sizeof(pk), "G10000;G2%u;%u;%u;5;10;0;0;G3PT7;KIM;G4EGD;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(pk, strlen(pk)); GUARDED(serialEvent()); run_loops(2);
        make_tag(a, 0x61, SCOPE_TYPE_TAG, 32767, "SC32767", "S32767");
        set_process(a, Process{0, 0, 0, 0, 0, false, 0, 0});
        logs_clear();
        touch(a);
        CHECK(lcd_has("Scope : 32767"), "V1 스코프 번호 5자리 표시가 잘리지 않는다");
    }

    // V2 P2-1: 이동 플래그는 Read Error 에 소모되지 않는다(재접촉이 이동으로)
    reboot_as('D');
    touch(mgr);
    {
        washed_scope(b, 0x62, 62);
        touch(b);                                        // 소독 시작
        sim_advance_ms(20UL * 60 * 1000);
        make_tag(c, 0x63, CLEAR_TYPE_TAG, 0, "", "");
        touch(c);                                        // 이동 플래그
        b.readErrBlock = SECTOR0_TAG; b.readErrTimes = 10;   // 이동 터치가 Read Error
        logs_clear();
        touch(b);
        CHECK(lcd_has("Read Error"), "V2 전제: 이동 터치 읽기 실패");
        b.readErrBlock = -1; b.readErrTimes = 0;
        logs_clear(); buzz_clear();
        touch(b);                                        // 재접촉
        const Process p = get_process(b);
        tlog("  V2 재접촉: MV=%u RW=%u\n", p.MovementNeeded, p.Rewrite);
        CHECK(p.MovementNeeded == 1, "V2 P2-1 읽기 실패 뒤 재접촉은 종료가 아니라 이동(플래그 유지)");
    }

    // V2 P3-1: 레거시 발급이 게이트웨이·검사명 블록도 비운다
    reboot_as('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        fresh_scope(c, 0x64, 64);
        set_record(c, SECTOR1_GATEWAY, 3, rel_date(8, 0, 0));
        memcpy(c.data[SECTOR15_EXAMINATION_SUBJECT], "OLDEXAM", 8);
        logs_clear();
        const char cmd[] = "S64;SER64;";
        serial_inject(cmd, sizeof(cmd) - 1);
        card_place(&c);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  V2 레거시 발급 뒤: gw년=%u 검사명=[%.3s] lcd=[%.30s]\n", get_ldt(c, SECTOR1_GATEWAY).Date.Year,
             (const char *)c.data[SECTOR15_EXAMINATION_SUBJECT], g_lcdLog);
        CHECK(lcd_has("new tag") && get_ldt(c, SECTOR1_GATEWAY).Date.Year == 0 && c.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0,
              "V2 P3-1 레거시 발급이 검사일시·검사명 블록을 비운다(JSON 발급과 같이)");
    }

    // V3 P2-1: 홈에서 SELECT 를 길게 눌러도 메뉴 첫 항목이 곧장 선택되지 않는다
    reboot_as('W');
    {
        g_btnIdleLimit = 2000000UL;
        deviceOption.SetNumber(1);
        const unsigned long t0 = millis();
        buttons_hold('S', t0 + 1300UL);                  // 1.3초 누름
        const int r = GUARDED(loop());                   // 메뉴 진입 → 무조작 → 60초 뒤 Exit
        buttons_hold(0, 0);
        tlog("  V3 S 길게: r=%d 번호=%d 경과=%lums\n", r, deviceOption.GetNumber(), millis() - t0);
        CHECK(r == 0 && deviceOption.GetNumber() == 1 && !lcd_has("Number ("),
              "V3 P2-1 SELECT 를 길게 눌러도 기기번호 편집으로 곧장 들어가지 않는다(메뉴 첫 화면에서 대기)");
    }

    // V3 P3-2: 자정을 끼고 시간 메뉴 저장 → 날짜가 하루 어긋나지 않는다
    {
        rtc_set(DateTime(2026, 9, 26, 23, 59, 50));
        buttons_script("L");                             // 저장 위치
        const unsigned long t0 = millis();
        buttons_at('S', t0 + 20000UL);                   // 20초 뒤(00:00:10) 저장 — 입력값은 진입 때 23:59:50
        GUARDED(ui.SetDeviceTime(rtc));
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  V3 자정 시간 저장: %u-%u-%u %02u:%02u\n", now.year(), now.month(), now.day(), now.hour(), now.minute());
        CHECK(now.day() == 26 && now.hour() == 23, "V3 P3-2 자정을 낀 시간 저장이 +24h 되지 않는다(전날 23:59)");
        rtc_set(rel_date(10, 0, 0));
    }

    // V3 P3-4: 177번지 손상값은 같은 판 재부팅에서도 정리된다
    {
        EEPROM.update(177, 0x7F);
        hard_reset(false, 0);
        tlog("  V3 177 손상값 뒤 pending=%u\n", disinfectionOption.GetClearPending());
        CHECK(disinfectionOption.GetClearPending() == DisinfectionOption::kPendingNone, "V3 P3-4 177번지 손상값은 같은 판 부팅에서도 정리");
    }

    // V4 P1: 더블터치 판정의 시작 블록 읽기 실패는 '종료' 가 아니라 Read Error(알람·기록 불변)
    reboot_as('W');
    touch(mgr);
    {
        fresh_scope(a, 0x71, 71);
        touch(a);                                        // 세척 시작(자동 종료 = +4분)
        const LocalDateTime autoEnd = get_ldt(a, SECTOR3_WASHING_END);
        sim_advance_ms(60UL * 1000);
        a.readErrBlock = SECTOR2_WASHING_START; a.readErrTimes = 10;   // 판정용 시작 블록만 못 읽음
        logs_clear(); buzz_clear();
        touch(a);
        const LocalDateTime after = get_ldt(a, SECTOR3_WASHING_END);
        tlog("  V4 판정 읽기 실패: lcd=[%.30s] 성공음=%u 알람=%d 종료분 %u→%u\n", g_lcdLog, buzz_count(50), rtc.HasAlarm(1),
             autoEnd.Time.Minute, after.Time.Minute);
        CHECK(lcd_has("Read Error") && buzz_count(50) == 0 && rtc.HasAlarm(1) && after.Time.Minute == autoEnd.Time.Minute,
              "V4 P1 더블터치 판정 읽기 실패 → Read Error · 알람 유지 · 1초짜리 종료 기록 없음");
        a.readErrBlock = -1; a.readErrTimes = 0;
    }
    reboot_as('D');
    touch(mgr);
    {
        washed_scope(b, 0x72, 72);
        touch(b);                                        // 소독 시작
        sim_advance_ms(60UL * 1000);
        b.readErrBlock = SECTOR5_DISINFECTION_START; b.readErrTimes = 10;
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  V4 소독 판정 읽기 실패: lcd=[%.30s] 알람=%d RW=%u\n", g_lcdLog, rtc.HasAlarm(2), get_process(b).Rewrite);
        CHECK(lcd_has("Read Error") && rtc.HasAlarm(2) && get_process(b).Rewrite == 2,
              "V4 P1 소독도 같은 규칙 — 판정 읽기 실패에 알람·상태 불변");
        b.readErrBlock = -1; b.readErrTimes = 0;
    }

    // ═══ 사장님 판정 2 (09-26) ═══
    // (1) 처리 직후(알림음 동안) 다른 태그로 바꿔 올리면 그 태그도 처리된다 · 같은 태그를 두면 1회만
    reboot_as('W');
    touch(mgr);
    {
        fresh_scope(a, 0x81, 81);
        fresh_scope(b, 0x82, 82);
        card_place(&a);
        run_loops(1);                                    // a 처리(시작)
        CHECK(get_process(a).Rewrite == 1, "판정1 전제: a 시작");
        card_place(&b);                                  // 치우지 않고 바로 교체 = 알림음 중 카드 교체
        run_loops(3);
        tlog("  판정1 교체: b RW=%u\n", get_process(b).Rewrite);
        CHECK(get_process(b).Rewrite == 1, "판정1 처리 직후 다른 태그로 바꿔 올리면 그 태그도 처리된다(무음 아님)");
        // 같은 태그를 계속 두면 1회만 — 종료로 재처리되지 않는다
        sim_advance_ms(5000);
        run_loops(30);
        card_remove(); run_loops(4);
        const int32_t wd = (DefaultRtc::ToDateTime(get_ldt(b, SECTOR3_WASHING_END)) -
                            DefaultRtc::ToDateTime(get_ldt(b, SECTOR2_WASHING_START))).totalseconds();
        tlog("  판정1 같은 태그 유지: 세척시간=%lds\n", (long)wd);
        CHECK(wd >= 4L * 60 && rtc.HasAlarm(1), "판정1 같은 태그를 올려 두면 1회만(종료로 재처리 안 됨)");
    }
    // (2) 게이트웨이는 세척만 하고 소독 안 한 스코프(WS=1·DS=0)를 거부한다
    reboot_as('G');
    {
        char pk[96];
        snprintf(pk, sizeof(pk), "G10000;G2%u;%u;%u;5;10;0;0;G3PT9;KIM;G4EGD;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(pk, strlen(pk)); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(c, 0x83, 83);
        set_process(c, Process{0, 0, 1, 0, 1, false, 0, 1});   // 세척만 끝남(WS=1·DS=0)
        logs_clear(); buzz_clear();
        touch(c);
        tlog("  판정2 WS=1 DS=0: NoComplete=%d Sm!=%d 600=%u\n", serial_has("No Complete"), serial_has("Sm!"), buzz_count(600));
        CHECK(serial_has("No Complete") && !serial_has("Sm!") && buzz_count(600) == 1 && get_process(c).Status == 0,
              "판정2 세척만 하고 소독 안 한 스코프는 게이트웨이가 거부(No Complete · 환자 안 붙임)");
        fresh_scope(c, 0x84, 84);                        // 과정에 안 들어간 스코프는 정상
        logs_clear();
        touch(c);
        CHECK(serial_has("Sm!") && get_process(c).Status == 1, "판정2 과정에 안 들어간 스코프는 정상 등록");
    }

    // ═══ W1 잠금 ═══
    // W1 P1: 방전된 시계를 처음 맞추는 조작(1월 1일 → 날짜 저장 → 14:30 입력)이 전날로 가지 않는다
    reboot_as('W');
    {
        rtc_set(DateTime(2026, 9, 28, 0, 0, 30));        // 방전(1월 1일)에서 날짜만 09-28 로 맞춘 직후(00:00:30) 라고 두고
        // 시간 메뉴에서 14:30:00 입력 후 저장 — 진입 스냅샷과 저장 시각이 같은 날 → 같은 날 14:30 이어야 한다
        char tbuf[8]; snprintf(tbuf, sizeof(tbuf), "143000");
        rtc.FromInternalString(tbuf, rtc.GetCurrentDateTime(), DefaultRtc::Format::Time);
        const DateTime n = rtc.GetCurrentDateTime();
        tlog("  W1 P1 같은 날 +14h: %u-%u-%u %02u:%02u\n", n.year(), n.month(), n.day(), n.hour(), n.minute());
        CHECK(n.month() == 9 && n.day() == 28 && n.hour() == 14 && n.minute() == 30,
              "W1 P1 같은 날에 12시간 넘게 맞추는 정당한 조작은 하루 옮기지 않는다");
        rtc_set(DateTime(2026, 9, 27, 22, 0, 0));        // 같은 날 22:00 → 09:00 (−13h) 도 같은 날(W2 C4)
        char tb3[8]; snprintf(tb3, sizeof(tb3), "090000");
        rtc.FromInternalString(tb3, rtc.GetCurrentDateTime(), DefaultRtc::Format::Time);
        CHECK(rtc.GetCurrentDateTime().day() == 27 && rtc.GetCurrentDateTime().hour() == 9,
              "W2 C4 같은 날 13시간 뒤로 맞추는 조작도 하루 옮기지 않는다");
        // 자정을 넘긴 편집(진입 23:59:50 → 저장 00:00:10, 입력 23:59:50)은 여전히 전날
        rtc_set(DateTime(2026, 9, 26, 23, 59, 50));
        const DateTime entry = rtc.GetCurrentDateTime();
        sim_advance_ms(20000);
        char tb2[8]; snprintf(tb2, sizeof(tb2), "235950");
        rtc.FromInternalString(tb2, entry, DefaultRtc::Format::Time);
        const DateTime m = rtc.GetCurrentDateTime();
        tlog("  W1 자정 넘긴 편집: %u-%u %02u:%02u\n", m.month(), m.day(), m.hour(), m.minute());
        CHECK(m.day() == 26 && m.hour() == 23, "W1 자정을 넘긴 편집은 진입 날짜(전날)로 — 종전 자정 보정 유지");
        // 자정을 넘긴 편집에서 **새 날의 시각**(00:00:05)을 넣으면 오늘 — "지금과 가까운 쪽" 의 다른 갈래(X1 P3-2)
        rtc_set(DateTime(2026, 9, 26, 23, 59, 50));
        const DateTime entry2 = rtc.GetCurrentDateTime();
        sim_advance_ms(20000);
        char tb4[8]; snprintf(tb4, sizeof(tb4), "000005");
        rtc.FromInternalString(tb4, entry2, DefaultRtc::Format::Time);
        CHECK(rtc.GetCurrentDateTime().day() == 27 && rtc.GetCurrentDateTime().hour() == 0,
              "W1 자정을 넘긴 편집에 새 날 시각을 넣으면 오늘(전날로 안 감)");
        rtc_set(rel_date(10, 0, 0));
    }
    // W1 P3-1: 담당자 미등록 상태에서도 첫 읽기 실패는 이동 플래그를 지킨다(거부에만 내림)
    reboot_as('D');
    {
        EEPROM.put(65, false);                           // 담당자 미등록(ManagerOption mFlagAddr)
        recordOption.SetManagerDisposability(false);
        washed_scope(b, 0x91, 91);
        touch(mgr);                                      // 담당자 등록 → 시작
        touch(b);
        sim_advance_ms(20UL * 60 * 1000);
        make_tag(c, 0x92, CLEAR_TYPE_TAG, 0, "", "");
        touch(c);                                        // 이동 플래그
        EEPROM.put(65, false);                           // 담당자 미등록으로 되돌림(조건식이 '거부' 로 오인하던 상태)
        b.readErrBlock = SECTOR0_TAG; b.readErrTimes = 10;   // 첫 읽기 실패
        logs_clear();
        touch(b);
        CHECK(lcd_has("Read Error"), "W1 전제: 담당자 미등록 + 첫 읽기 실패 = Read Error");
        b.readErrBlock = -1; b.readErrTimes = 0;
        touch(mgr);                                      // 담당자 다시 등록
        logs_clear();
        touch(b);                                        // 재접촉 → 이동이어야
        tlog("  W1 P3-1 재접촉: MV=%u\n", get_process(b).MovementNeeded);
        CHECK(get_process(b).MovementNeeded == 1, "W1 P3-1 담당자 미등록 상태의 읽기 실패도 이동 플래그를 지킨다");
        // is_valid 의 세 번째 갈래(Process 블록 읽기 실패)도 -1 — 이동 플래그 유지(X1 P3-4)
        washed_scope(b, 0x93, 93);
        touch(b);
        sim_advance_ms(20UL * 60 * 1000);
        touch(c);                                        // 이동 플래그
        b.readErrBlock = SECTOR1_PROCESS; b.readErrTimes = 10;
        logs_clear();
        touch(b);
        CHECK(lcd_has("Read Error"), "W1 전제: Process 읽기 실패 = Read Error");
        b.readErrBlock = -1; b.readErrTimes = 0;
        logs_clear();
        touch(b);
        CHECK(get_process(b).MovementNeeded == 1, "X1 P3-4 Process 블록 읽기 실패(세 번째 갈래)도 이동 플래그를 지킨다");
    }
    // W1 P3-2: 본체번호 4자리는 숫자만 5칸
    reboot_as('G');
    {
        char pk[96];
        snprintf(pk, sizeof(pk), "G19999;G2%u;%u;%u;5;10;0;0;G3PT1;A;G4EGD;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(pk, strlen(pk)); GUARDED(serialEvent()); run_loops(1);
        ui.InvalidateHome(); logs_clear(); run_loops(1);
        CHECK(lcd_has(" 9999") && !lcd_has("G:9999"), "W1 P3-2 본체번호 4자리는 숫자만 5칸(20열 밖으로 안 나감)");
    }

    // ═══ X2 잠금 ═══
    reboot_as('D');
    touch(mgr);
    {
        // X2 P3-1: 소독 횟수 32767 에서 소독을 시작해도 0 으로 돌지 않고 "MaxCount Over" 가 유지된다
        disinfectionOption.SetMaximumCount(100);
        disinfectionOption.SetCount(32767);
        washed_scope(b, 0x94, 94);
        logs_clear();
        touch(b);
        tlog("  X2 P3-1 count=%d\n", disinfectionOption.GetCount());
        CHECK(disinfectionOption.GetCount() == 32767 && lcd_has("MaxCount Over"),
              "X2 P3-1 소독 횟수 32767 에서 +1 은 0 으로 돌지 않는다(MaxCount Over 유지)");
        disinfectionOption.SetCount(0);
        disinfectionOption.SetMaximumCount(0);
        // X2 P2-1(판정 · 1.0 유지): 동기된 소독기도 세척기가 세척 시간 넘게 앞서면 "세척 종료 + 1분" 으로 따라간다
        rtc_set(rel_date(10, 0, 0));
        fresh_scope(b, 0x95, 95);
        set_process(b, Process{1, 0, 1, 0, 1, false, 0, 0});
        set_record(b, SECTOR2_WASHING_START, 1, rel_date(10, 30, 0));   // 세척기가 30분 앞선다
        set_record(b, SECTOR3_WASHING_END,   1, rel_date(10, 45, 0));
        touch(b);
        const DateTime n = rtc.GetCurrentDateTime();
        tlog("  X2 P2-1 세척기 앞섬: %02u:%02u\n", n.hour(), n.minute());
        CHECK(n.hour() == 10 && n.minute() == 46, "X2 P2-1 [판정] 동기된 소독기도 앞선 세척기를 따라간다(세척 종료+1분 · 1.0)");
        rtc_set(rel_date(10, 0, 0));
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
