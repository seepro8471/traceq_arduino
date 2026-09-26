// 4차 감사 후속(09-26) 잠금 — 서버 연결 확인 peek · 담당자 정본 하나 · 쓰기 검증 정본 하나 ·
// 게이트웨이 검사일시 이월 금지(2.2.5 M45) · guest 판정은 대입(2.2.5 M46) · 스코프 번호가 직전 것으로 안 나감.
#include "common.h"

static SimCard mgr, a, b;

static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); }
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 0, 0, 0, 0, false, 0, 0});
}
static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}
static void gw_scope(SimCard &t, uint8_t uid, int no)      // 게이트웨이 대상: 환자정보 없음(Status=0)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static bool same16(const SimCard &x, uint8_t bx, const SimCard &y, uint8_t by)
{
    return memcmp(x.data[bx], y.data[by], 16) == 0;
}
static void done_scope(SimCard &t, uint8_t uid, int no)   // 서버 덤프 대상(W·D 완료)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END,   2, rel_date(9, 23, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));

    // ── ① 서버 연결 확인은 줄 서 있던 PC 명령을 먹지 않는다 ──
    boot('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);   // 인증
    {
        // (a) 'Z' 앞에 설정 요청 JSON 이 줄 서 있음 — 종전엔 24바이트가 소거됐다.
        done_scope(a, 0x11, 11);
        alarmOption.SetTimeSlot1(30);
        const char q[] = "{\"cmd\":\"cfg_set_config\",\"washing_time\":12}";
        logs_clear();
        serial_inject(q, sizeof(q) - 1);
        touch(a);                                  // PSOk → peek 가 '{' 를 봄 → 곧장 Not Connected
        tlog("  ①a JSON 앞: Ok!=%d NotConnected=%d\n", serial_has("Ok!"), serial_has("Not Connected"));
        CHECK(!serial_has("Ok!"), "①a 'Z' 앞에 명령이 있으면 덤프하지 않는다(먹지 않고 남긴다)");
        GUARDED(serialEvent()); run_loops(2);      // 아두이노 코어가 loop 사이에 serialEvent 를 부른다
        tlog("  ①a 다음 루프 뒤 세척시간=%d (12 여야 — 명령이 살아 있었다)\n", alarmOption.GetTimeSlot1());
        CHECK(alarmOption.GetTimeSlot1() == 12, "①a 줄 서 있던 설정 명령이 유실되지 않고 처리된다");
        // 재접촉 → 정상 덤프
        logs_clear();
        serial_inject("Z", 1);
        touch(a);
        CHECK(serial_has("Ok!"), "①a 재접촉하면 정상 덤프");
    }
    {
        // (b) 정상: 'Z' 하나면 곧장 덤프 · 'Z' 는 소비되어 다음에 남지 않는다
        done_scope(b, 0x12, 12);
        logs_clear();
        serial_inject("Z", 1);
        touch(b);
        tlog("  ①b Z: Ok!=%d 남은 바이트=%d\n", serial_has("Ok!"), Serial.available());
        CHECK(serial_has("Ok!") && Serial.available() == 0, "①b 'Z' 로 덤프하고 'Z' 만 소비한다");
    }

    // ── ② 담당자 키·이름 규칙은 세척·소독 여덟 블록이 같은 정본에서 온다 ──
    reboot_as('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND014567890123", "KIMJH");   // ID 14자 전부 채움 — 마지막 바이트까지 규칙이 보이게
    touch(mgr);
    fresh_scope(a, 0x21, 21);
    touch(a);
    sim_advance_ms(60UL * 1000);
    touch(a);                                      // 세척 종료
    reboot_as('D');
    touch(mgr);
    washed_scope(b, 0x22, 22);
    touch(b);
    sim_advance_ms(60UL * 1000);
    touch(b);                                      // 소독 종료
    CHECK(same16(a, SECTOR3_WASHING_START_MANAGER_KEY,  b, SECTOR5_DISINFECTION_START_MANAGER_KEY) &&
          same16(a, SECTOR4_WASHING_END_MANAGER_KEY,    b, SECTOR6_DISINFECTION_END_MANAGER_KEY),
          "②세척·소독 담당자 키가 같은 정본으로 쓰인다(시작·종료)");
    CHECK(same16(a, SECTOR3_WASHING_START_MANAGER_NAME, b, SECTOR5_DISINFECTION_START_MANAGER_NAME) &&
          same16(a, SECTOR4_WASHING_END_MANAGER_NAME,   b, SECTOR6_DISINFECTION_END_MANAGER_NAME),
          "②세척·소독 담당자 이름이 같은 정본으로 쓰인다(시작·종료)");

    // ── ③ guest 판정은 대입 — 종료 없이 회수된 guest 슬롯이 며칠 뒤 단독 소독을 guest 로 만들지 않는다(M46) ──
    {
        disinfectionOption.SetSimultaneousDisinfectionSlot(2);
        disinfectionOption.SetSimultaneousDisinfectionDelay(5);   // host 시작 5분 안이면 guest
        rtc_set(rel_date(11, 0, 0));
        washed_scope(a, 0x23, 23);
        touch(a);                                  // host
        sim_advance_ms(60UL * 1000);
        washed_scope(b, 0x24, 24);
        touch(b);                                  // 1분 뒤 → guest(횟수 안 오름)
        const int cntAfterGuest = disinfectionOption.GetCount();
        // b 는 종료 터치 없이 회수(슬롯에 번호가 남는다). 이틀 뒤 b 단독 소독.
        rtc_set(DateTime(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY + 2, 9, 0, 0));
        washed_scope(b, 0x24, 24);
        touch(b);
        tlog("  ③이틀 뒤 단독: 횟수 %d → %d\n", cntAfterGuest, disinfectionOption.GetCount());
        CHECK(disinfectionOption.GetCount() == cntAfterGuest + 1,
              "③guest 슬롯에 남아 있던 스코프도 시간창 밖 단독 소독이면 host(횟수 +1)");
        disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    }

    // ── ④ 게이트웨이: 검사일시(G2) 없는 패킷은 이전 검사일시를 이월하지 않는다(M45) · 일괄 쓰기 검증 ──
    reboot_as('G');
    rtc_set(rel_date(10, 0, 0));
    {
        char p1[96];
        snprintf(p1, sizeof(p1), "G10000;G2%u;%u;%u;5;10;0;0;G3PT001;HONG;G4EGD;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(p1, strlen(p1)); GUARDED(serialEvent()); run_loops(2);
        gw_scope(a, 0x31, 31);
        touch(a);
        CHECK(get_ldt(a, SECTOR1_GATEWAY).Time.Hour == 10, "④전제: 완전 패킷 → 검사일시 10시");

        const char p2[] = "G10000;G3PT002;KIM;G4COL;;;G5;";              // G2 없음
        serial_inject(p2, sizeof(p2) - 1); GUARDED(serialEvent()); run_loops(2);
        gw_scope(b, 0x32, 32);
        touch(b);
        const LocalDateTime g = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  ④G2 없음: 환자=[%.5s] 검사일시 년=%u 시=%u\n", (const char *)b.data[SECTOR2_PATIENT_KEY], g.Date.Year, g.Time.Hour);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "PT002", 5) == 0 && g.Date.Year == 0 && g.Time.Hour == 0,
              "④G2 없는 패킷 → 새 환자는 쓰되 이전 검사일시(10시)를 이월하지 않는다");
    }
    {
        // 일괄 쓰기(검사명 3블록)의 확인 읽기만 실패 → 검증이 죽어 있으면 성공음+Status=1 이 난다(E P2-2).
        serial_inject("G10000;G3PT003;LEE;G4EGD;;;G5;", 30); GUARDED(serialEvent()); run_loops(2);
        gw_scope(a, 0x33, 33);
        a.readErrBlock = SECTOR15_EXAMINATION_SUBJECT;
        a.readErrSkip  = 1;                                  // ClearSector(15) 의 확인 읽기는 통과
        a.readErrTimes = 10;                                 // WriteBlocks 의 확인 읽기는 재시도까지 전부 실패
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  ④일괄 검증 실패: Status=%u Sm!=%d 50=%u lcd=[%.30s]\n", get_process(a).Status, serial_has("Sm!"), buzz_count(50), g_lcdLog);
        CHECK(get_process(a).Status == 0 && !serial_has("Sm!") && buzz_count(50) == 0 && lcd_has("Write Error"),
              "④일괄 쓰기 검증 실패 → Status 안 섬·Sm! 없음·성공음 없음·Write Error");
    }

    // ── ⑤ 스코프 번호 읽기가 실패하면 직전 스코프의 번호가 이 환자 것으로 PC 에 나가지 않는다(E P2-3) ──
    {
        serial_inject("G10000;G3PT004;PARK;G4EGD;;;G5;", 31); GUARDED(serialEvent()); run_loops(2);
        gw_scope(a, 0x34, 61);                            // 번호 61 → 프레임에 03023D00
        logs_clear();
        touch(a);
        CHECK(serial_has("03023D00") && serial_has("Sm!"), "⑤전제: 스코프 61 프레임(0302 + 3D00)");
        serial_inject("G10000;G3PT005;CHOI;G4EGD;;;G5;", 31); GUARDED(serialEvent()); run_loops(2);
        gw_scope(b, 0x35, 71);
        b.readErrBlock = SECTOR0_TAG;                        // 번호 블록 읽기 실패
        b.readErrTimes = 10;
        logs_clear();
        touch(b);
        tlog("  ⑤번호 읽기 실패: Sm!=%d 직전61=%d Status=%u\n", serial_has("Sm!"), serial_has("03023D00"), get_process(b).Status);
        CHECK(!serial_has("Sm!") && !serial_has("03023D00") && get_process(b).Status == 0,
              "⑤번호 읽기 실패 → 직전 스코프(61) 번호가 PC 로 나가지 않고 기록도 안 한다");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
