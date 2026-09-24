// 실패를 조용히 넘기지 않는다 — 네 갈래(세척·소독·게이트웨이·서버) 전부에서
// 기록/읽기 실패 시 LCD 표시 + 경고음 4회, 성공음·알람·커밋은 없다 (2.2.9).
// ★카드는 4장만 두고 돌려쓴다(make_tag 이 카드를 완전히 초기화한다). 더 늘리면 시뮬 RAM 이 넘친다.
#include "common.h"

static SimCard mgr, a, b, c;

static void reboot_as(char type)
{
    deviceOption.SetType(type);
    hard_reset(false);
}
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void washed_scope(SimCard &t, uint8_t uid, int no)   // 세척 끝난 상태(소독기 입력용)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 23, 9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, DateTime(2026, 9, 23, 9, 4, 0));
}
// 소리 규칙(사장님 09-23) — 부저는 한 소리뿐이라 리듬으로 가른다.
//   정상 = 짧게 1회(50) · 환자정보 없음(기록됨) = 길게 2회(400)
//   실패(다시 대면 됨) = 짧게 4회(100) · 거부(다시 대도 안 됨) = 삐삐(60x2) + 긴 삐(600)
static bool sound_ok(uint8_t shortOnce, uint8_t longTwice, uint8_t fail)
{
    return buzz_count(50) == shortOnce && buzz_count(400) == longTwice &&
           buzz_count(100) == fail && buzz_count(600) == 0;
}
static bool rejected()      // 거부 리듬만 울렸는가
{
    return buzz_count(60) == 2 && buzz_count(600) == 1 &&
           buzz_count(50) == 0 && buzz_count(100) == 0 && buzz_count(400) == 0;
}
// 실패 알림이 갖춰졌는가 — 표시 + 실패음 4회 + 정상·환자정보없음 소리 0
static bool warned(const char *text, uint16_t successMs)
{
    return lcd_has(text) && sound_ok(0, 0, 4) && buzz_count(successMs) == 0;
}

int main()
{
    rtc_set(DateTime(2026, 9, 23, 10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── 담당자 등록: 태그 읽기 실패 ──
    {
        make_tag(a, 0x02, MANAGER_TYPE_TAG, 8, "ND02000", "LEE");
        a.readErrBlock = SECTOR0_TAG;
        a.readErrTimes = 3;
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  담당자 읽기 실패: lcd=[%.60s] 경고음=%u\n", g_lcdLog, buzz_count(100));
        CHECK(warned("Read Error", 50), "담당자 등록 읽기 실패 → Read Error + 경고음");
    }

    // ── 스코프 태그 읽기 실패(세척기) — 회사코드는 읽혔는데 그 뒤 블록이 안 읽힌다 ──
    {
        fresh_scope(a, 0x11, 11);
        a.readErrBlock = SECTOR1_TAG_SERIAL;            // is_valid 안에서 실패
        a.readErrTimes = 5;
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  세척 읽기 실패: lcd=[%.40s] 100=%u 50=%u\n", g_lcdLog, buzz_count(100), buzz_count(50));
        CHECK(warned("Read Error", 50), "세척기 스코프 읽기 실패 → Read Error + 실패음");
        CHECK(get_process(a).Rewrite == 0, "세척기 읽기 실패 → 아무것도 기록하지 않는다");
    }

    // ── 세척 종료 기록 실패 ──
    {
        fresh_scope(a, 0x21, 21);
        touch(a);                                       // 정상 시작
        CHECK(get_process(a).Rewrite == 1 && rtc.HasAlarm(1), "회귀: 세척 시작 정상");
        sim_advance_ms(60UL * 1000);                    // 알람(4분) 전에 종료 — 알람 유지 확인용
        a.failWriteAt = a.writeCount + 2;               // 종료 기록(1) 은 됐고 담당자 키(2) 에서 실패
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  세척 종료 실패: lcd=[%.60s] 경고음=%u 성공음=%u 알람=%d\n",
             g_lcdLog, buzz_count(100), buzz_count(50), rtc.HasAlarm(1));
        CHECK(warned("Write Error", 50), "세척 종료 실패 → Write Error + 경고음, 성공음 없음");
        CHECK(rtc.HasAlarm(1), "세척 종료 실패 → 알람 유지(재접촉 유도)");
        CHECK(get_process(a).Rewrite == 1, "세척 종료 실패 → 태그는 여전히 시작 상태");
    }

    // ── 소독 종료 기록 실패 ──
    reboot_as('D');
    touch(mgr);
    {
        washed_scope(b, 0x22, 22);
        touch(b);                                       // 정상 시작
        CHECK(get_process(b).Rewrite == 2 && rtc.HasAlarm(2), "회귀: 소독 시작 정상");
        sim_advance_ms(5UL * 60 * 1000);                // 알람(18분) 전에 종료
        b.failWriteAt = b.writeCount + 2;               // 종료 기록(1) 뒤 담당자 키(2) 에서 실패
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  소독 종료 실패: lcd=[%.60s] 경고음=%u 성공음=%u 알람=%d\n",
             g_lcdLog, buzz_count(100), buzz_count(50), rtc.HasAlarm(2));
        CHECK(warned("Write Error", 50), "소독 종료 실패 → Write Error + 경고음, 성공음 없음");
        CHECK(rtc.HasAlarm(2), "소독 종료 실패 → 알람 유지");
        CHECK(get_process(b).Rewrite == 2, "소독 종료 실패 → 태그는 여전히 시작 상태");
    }

    // ── 스코프 태그 읽기 실패(소독기) ──
    {
        washed_scope(a, 0x12, 12);
        a.readErrBlock = SECTOR1_TAG_SERIAL;
        a.readErrTimes = 5;
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  소독 읽기 실패: lcd=[%.40s] 100=%u\n", g_lcdLog, buzz_count(100));
        CHECK(warned("Read Error", 50), "소독기 스코프 읽기 실패 → Read Error + 실패음");
        CHECK(get_process(a).Rewrite == 0 && get_process(a).DisinfectionCount == 0,
              "소독기 읽기 실패 → 아무것도 기록하지 않는다");
    }

    // ── 소독기 이동 기록 실패(클리어 태그로 이동 플래그를 세운 뒤 종료 터치) ──
    {
        washed_scope(c, 0x23, 23);
        touch(c);                                       // 소독 시작
        sim_advance_ms(20UL * 60 * 1000);
        make_tag(a, 0x24, CLEAR_TYPE_TAG, 0, "", "");
        touch(a);                                       // 이동 플래그
        c.failWriteAt = c.writeCount + 1;
        logs_clear(); buzz_clear();
        touch(c);
        tlog("  이동 실패: lcd=[%.60s] 경고음=%u 성공음=%u\n", g_lcdLog, buzz_count(100), buzz_count(50));
        CHECK(warned("Write Error", 50), "소독 이동 실패 → Write Error + 경고음, 성공음 없음");
    }

    // ── 게이트웨이: 환자정보 없이 기록(폴백)하다 실패 ──
    //    ★폴백을 먼저 — 환자정보는 한 번 받으면 계속 남으므로(사장님 09-23 판정 · 재론 금지)
    //      패킷을 넣은 뒤에는 폴백 경로로 들어갈 수 없다.
    reboot_as('G');
    {
        fresh_scope(b, 0x26, 26);
        b.failWriteAt = 1;
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  폴백 실패: lcd=[%.60s] 경고음=%u 폴백완료음=%u\n",
             g_lcdLog, buzz_count(100), buzz_count(40));
        CHECK(serial_has("Not Patient Info"), "폴백 경로를 탔다");
        CHECK(warned("Write Error", 40), "게이트웨이 폴백 기록 실패 → Write Error + 경고음, 완료음 없음");
    }

    // ── 게이트웨이: 환자정보 기록 실패 ──
    {
        const char pkt[] = "G10000;G22026;9;23;3;10;0;0;G3A0001;\xC8\xAB\xB1\xE6\xB5\xBF;G4EGD;;;G5;";
        serial_inject(pkt, sizeof(pkt) - 1);
        GUARDED(serialEvent());
        run_loops(2);
        fresh_scope(c, 0x25, 25);
        c.failWriteAt = 1;                              // 첫 쓰기(게이트웨이 블록)부터 실패
        logs_clear(); buzz_clear();
        touch(c);
        tlog("  게이트웨이 실패: lcd=[%.60s] 경고음=%u 성공음=%u Sm!=%d\n",
             g_lcdLog, buzz_count(100), buzz_count(50), serial_has("Sm!"));
        CHECK(warned("Write Error", 50), "게이트웨이 기록 실패 → Write Error + 경고음, 성공음 없음");
        CHECK(!serial_has("Sm!"), "게이트웨이 기록 실패 → Sm! 안 보냄(PC 가 미기록을 안다)");
        CHECK(get_process(c).Status == 0, "게이트웨이 기록 실패 → 환자정보 있음 플래그 안 섬");
    }

    // ── 게이트웨이: 태그 읽기 실패(세척·소독기와 같은 알림이어야 한다) ──
    {
        fresh_scope(a, 0x27, 27);
        a.readErrBlock = SECTOR1_PROCESS;
        a.readErrTimes = 3;
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  게이트웨이 읽기 실패: lcd=[%.60s] 경고음=%u\n", g_lcdLog, buzz_count(100));
        CHECK(warned("Read Error", 50), "게이트웨이 읽기 실패 → Read Error + 경고음(형제와 같은 알림)");
    }

    // ── 서버: 덤프 뒤 초기화가 **어느 쓰기에서 실패하든** 알리고, 재접촉으로 복구된다 ──
    //    덤프 자체가 Clear 2회(블록 18·52)를 쓰므로 그 뒤 쓰기는 3번째부터. 소거·Process 를 모두 덮는다.
    reboot_as('S');
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    {
        // 서버도 태그 읽기 실패는 같은 알림
        fresh_scope(a, 0x13, 13);
        a.readErrBlock = SECTOR1_TAG_SERIAL;
        a.readErrTimes = 5;
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(a);
        tlog("  서버 읽기 실패: lcd=[%.40s] 100=%u 150=%u\n", g_lcdLog, buzz_count(100), buzz_count(150));
        CHECK(warned("Read Error", 150), "서버 스코프 읽기 실패 → Read Error + 실패음");
        CHECK(!serial_has("Ok!"), "서버 읽기 실패 → 덤프를 보내지 않는다");
    }
    for (uint8_t off = 3; off <= 10; ++off)   // 덤프 뒤 쓰기 8개(Process·게이트웨이·섹터15 x3·커밋·환자 x2) 전수
    {
        fresh_scope(b, static_cast<uint8_t>(0x28 + off), 28 + off);
        set_process(b, Process{1, 1, 1, 1, 1, false, 0, 2});
        set_record(b, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 23, 9, 0, 0));
        set_record(b, SECTOR3_WASHING_END,   1, DateTime(2026, 9, 23, 9, 4, 0));
        set_record(b, SECTOR5_DISINFECTION_START, 2, DateTime(2026, 9, 23, 9, 5, 0));
        set_record(b, SECTOR6_DISINFECTION_END,   2, DateTime(2026, 9, 23, 9, 23, 0));
        memcpy(b.data[SECTOR2_PATIENT_KEY], "P0001", 6);
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        b.failWriteAt = b.writeCount + off;
        touch(b);
        const Process p = get_process(b);
        tlog("  서버 off=%u: lcd=[%.40s] 경고음=%u 완료음=%u WS=%u DC=%u\n",
             off, g_lcdLog, buzz_count(100), buzz_count(150), p.WashingStatus, p.DisinfectionCount);
        CHECK(serial_has("Ok!"), "서버: 덤프 자체는 나갔다(와이어 불변)");
        CHECK(warned("Write Error", 150), "서버 초기화 실패 → Write Error + 경고음, 완료음 없음");
        // ★"환자정보 있음(Status=1)인데 환자 블록은 빈" 태그가 남으면 세척기의 미기재 경고가 무력화된다.
        CHECK(!(p.Status == 1 && b.data[SECTOR2_PATIENT_KEY][0] == 0),
              "서버 초기화 실패 → '환자정보 있음 + 빈 블록' 태그를 남기지 않는다");
        // ★재접촉이 가능하면(W/D 남음) 2차 덤프가 나간다 — 그 덤프에 환자키가 비어 있으면 PC 의
        //  이미 저장된 검사기록을 빈 값으로 덮어쓴다. 되므로 "재덤프 가능 → 환자키 살아 있음" 이어야 한다.
        const bool redumpable = (p.WashingStatus != 0 && p.DisinfectionCount != 0);
        CHECK(!redumpable || b.data[SECTOR2_PATIENT_KEY][0] != 0,
              "서버 초기화 실패 → 재덤프가 가능하면 환자키가 살아 있다(빈 덤프로 덮어쓰기 금지)");
        b.failWriteAt = 0;
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(b);
        const Process p2 = get_process(b);
        tlog("    재접촉: WS=%u 환자키=[%.5s]\n", p2.WashingStatus, (const char *)b.data[SECTOR2_PATIENT_KEY]);
        // 재덤프가 가능한 창(커밋 전 실패)에서만 재접촉으로 소거까지 끝난다. 커밋 뒤(환자 블록) 실패는
        // 이미 완료 처리돼 재접촉이 거부되고 환자정보가 남는다 — 2.2.8 도 같아 회귀는 아니다.
        CHECK(p2.WashingStatus == 0 && p2.Status == 0, "서버 초기화 실패 → 완료 처리는 유지된다");
        CHECK(!redumpable || b.data[SECTOR2_PATIENT_KEY][0] == 0,
              "서버 초기화 실패 → 재덤프 가능했으면 재접촉으로 환자정보까지 지워진다");
    }

    // ── 서버: 소거만 실패하고 Process 쓰기는 되는 경우(카드는 살아 있고 확인 읽기만 실패) ──
    //    쓰기 실패 주입은 카드를 죽여 뒤 쓰기도 같이 실패하므로, 소거 단독 실패는 이 방법으로만 재현된다.
    {
        fresh_scope(b, 0x39, 39);
        set_process(b, Process{1, 1, 1, 1, 1, false, 0, 2});
        set_record(b, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 23, 9, 0, 0));
        set_record(b, SECTOR3_WASHING_END,   1, DateTime(2026, 9, 23, 9, 4, 0));
        set_record(b, SECTOR5_DISINFECTION_START, 2, DateTime(2026, 9, 23, 9, 5, 0));
        set_record(b, SECTOR6_DISINFECTION_END,   2, DateTime(2026, 9, 23, 9, 23, 0));
        memcpy(b.data[SECTOR2_PATIENT_KEY], "P0002", 6);
        b.readErrBlock = SECTOR1_GATEWAY;
        b.readErrSkip  = 1;                             // 덤프의 블록 5 읽기는 통과
        b.readErrTimes = 10;                            // 소거 뒤 확인 읽기는 재시도까지 전부 실패
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(b);
        const Process p = get_process(b);
        tlog("  서버 소거만 실패: lcd=[%.40s] 경고음=%u 완료음=%u WS=%u\n",
             g_lcdLog, buzz_count(100), buzz_count(150), p.WashingStatus);
        CHECK(warned("Write Error", 150), "서버 소거 실패 → Write Error + 경고음, 완료음 없음");
        CHECK(p.WashingStatus != 0, "서버 소거 실패 → 재접촉이 가능한 상태로 보존");
        // ★소거가 실패했는데 완료 플래그만 지워지면 재접촉이 "Not W and D" 로 거부되어 환자정보가 영구히 남는다.
        b.readErrBlock = -1; b.readErrTimes = 0;
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(b);
        tlog("  재접촉 뒤: WS=%u 환자키=[%.6s]\n", get_process(b).WashingStatus,
             (const char *)b.data[SECTOR2_PATIENT_KEY]);
        CHECK(get_process(b).WashingStatus == 0 && b.data[SECTOR2_PATIENT_KEY][0] == 0,
              "서버 소거 실패 → 재접촉하면 환자정보까지 실제로 지워진다");
    }

    // ── 거부는 실패와 다른 리듬(삐삐 + 긴 삐) — 화면을 못 봐도 갈린다 ──
    reboot_as('D');
    {
        // 담당자 미등록 거부
        eeprom_factory();
        hard_reset(false);
        deviceOption.SetType('D');
        hard_reset(false);
        fresh_scope(a, 0x51, 51);
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  거부(담당자): lcd=[%.40s] 60=%u 600=%u 100=%u\n",
             g_lcdLog, buzz_count(60), buzz_count(600), buzz_count(100));
        CHECK(lcd_has("No Manager Info") && rejected(), "거부: 담당자 미등록 = 삐삐 + 긴 삐");

        touch(mgr);
        // 세척 안 한 스코프 거부
        fresh_scope(b, 0x52, 52);
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  거부(세척 안 함): lcd=[%.40s] 60=%u 600=%u\n", g_lcdLog, buzz_count(60), buzz_count(600));
        CHECK(lcd_has("No Washing Info") && rejected(), "거부: 세척 안 한 스코프 = 삐삐 + 긴 삐");
    }
    reboot_as('S');
    serial_inject("Z", 1);              // PSOk→Z 인증(리셋으로 풀렸다)
    GUARDED(serialEvent());
    run_loops(1);
    {
        // 서버: 세척·소독 안 된 태그 거부
        fresh_scope(a, 0x53, 53);
        logs_clear(); buzz_clear();
        serial_inject("Z", 1);
        touch(a);
        tlog("  거부(서버): 60=%u 600=%u serial=%d\n", buzz_count(60), buzz_count(600), serial_has("Not W and D"));
        CHECK(serial_has("Not W and D"), "서버 거부 문자열은 그대로(PC 음성 계약)");
        CHECK(rejected(), "거부: 서버 미완료 태그 = 삐삐 + 긴 삐");
    }

    // ── 진입 단계도 같은 규칙: 읽기 실패는 실패음, 엉뚱한 종류의 태그는 거부음 ──
    reboot_as('G');
    {
        make_tag(a, 0x54, MANAGER_TYPE_TAG, 9, "ND09", "LEE");   // 게이트웨이엔 담당자 태그를 댈 수 없다
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  진입 거부: serial=%d 60=%u 600=%u\n", serial_has("Invalid Tag Type"),
             buzz_count(60), buzz_count(600));
        CHECK(serial_has("Invalid Tag Type") && rejected(), "거부: 엉뚱한 종류의 태그 = 삐삐 + 긴 삐");

        fresh_scope(b, 0x55, 55);
        b.readErrBlock = SECTOR0_COMPANY;                        // 회사코드 읽기 자체가 실패
        b.readErrTimes = 5;
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  진입 읽기 실패: 100=%u 60=%u\n", buzz_count(100), buzz_count(60));
        CHECK(buzz_count(100) == 4 && buzz_count(60) == 0, "실패: 진입 읽기 실패 = 짧게 4회");
    }

    // ── 소리 규칙 전수: 같은 뜻이면 기기가 달라도 같은 소리 ──
    reboot_as('G');
    {
        fresh_scope(a, 0x41, 41);
        buzz_clear(); touch(a);                         // 환자정보 미수신 → 폴백 기록
        CHECK(sound_ok(0, 2, 0), "규칙: 게이트웨이 환자정보 없음 = 길게 2회");

        const char pkt[] = "G10000;G22026;9;23;3;11;0;0;G3A0002;\xC8\xAB\xB1\xE6\xB5\xBF;G4EGD;;;G5;";
        serial_inject(pkt, sizeof(pkt) - 1);
        GUARDED(serialEvent());
        run_loops(2);
        fresh_scope(b, 0x42, 42);
        buzz_clear(); touch(b);
        CHECK(sound_ok(1, 0, 0), "규칙: 게이트웨이 정상 = 짧게 1회");
    }
    reboot_as('W');
    touch(mgr);
    {
        fresh_scope(a, 0x43, 43);
        buzz_clear(); touch(a);
        CHECK(sound_ok(0, 2, 0), "규칙: 세척 환자정보 없음 = 길게 2회(게이트웨이와 같다)");

        fresh_scope(b, 0x44, 44);
        set_process(b, Process{1, 0, 0, 0, 0, false, 0, 0});   // Status=1 → 환자정보 있음
        buzz_clear(); touch(b);
        CHECK(sound_ok(1, 0, 0), "규칙: 세척 정상 = 짧게 1회");
    }
    reboot_as('D');
    touch(mgr);
    {
        washed_scope(a, 0x45, 45);
        set_process(a, Process{0, 0, 1, 0, 1, false, 0, 0});   // Status=0 → 환자정보 없음
        buzz_clear(); touch(a);
        CHECK(sound_ok(0, 2, 0), "규칙: 소독 환자정보 없음 = 길게 2회(세척기와 같다)");

        washed_scope(b, 0x46, 46);                             // Status=1 → 환자정보 있음
        buzz_clear(); touch(b);
        CHECK(sound_ok(1, 0, 0), "규칙: 소독 정상 = 짧게 1회");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
