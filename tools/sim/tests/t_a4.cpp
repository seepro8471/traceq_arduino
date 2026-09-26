// 4차 감사(09-25) 제품 수정 11자리 잠금 — 각 자리를 되돌리면 여기서 빨강이 나야 한다.
// 카드 4장 돌려쓰기(시뮬 RAM). 판정은 ==> 숫자 + 실패 목록 절로.
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
    set_record(t, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 25, 9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, DateTime(2026, 9, 25, 9, 4, 0));
}
static int32_t dur_s(const SimCard &t, uint8_t startBlk, uint8_t endBlk)
{
    return (DefaultRtc::ToDateTime(get_ldt(t, endBlk)) - DefaultRtc::ToDateTime(get_ldt(t, startBlk))).totalseconds();
}
static void json(const char *s) { serial_inject(s, strlen(s)); GUARDED(serialEvent()); }

int main()
{
    rtc_set(DateTime(2026, 9, 25, 10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── ① 리더 재초기화가 올려 둔 태그를 다시 처리하지 않는다(B P2-1) ──
    //    죽은 리더 → Reinitialize → 소프트 리셋으로 카드 전원 상실(정지 풀림). 그래도 present 표시가 남아
    //    같은 태그는 KeepAlive 다. 옛 코드는 표시를 지워 '새 태그' 로 보고 3초 뒤 종료로 기록했다.
    {
        fresh_scope(a, 0x21, 21);
        card_place(&a);
        run_loops(1);                                    // 세척 시작(태그를 계속 올려 둔 상태)
        CHECK(get_process(a).Rewrite == 1 && rtc.HasAlarm(1), "①전제: 세척 시작 + 알람");
        sim_advance_ms(3000);                            // 3초 — 더블터치 창(2초) 밖
        g_versionReg = 0x00;                             // 리더 죽음 → 다음 루프 Reinitialize
        run_loops(1);
        g_versionReg = 0x92;
        run_loops(30);                                   // 카드는 계속 올려 둔 채 여러 루프
        card_remove(); run_loops(4);
        tlog("  ①재초기화: fieldDrop=%u RW=%u 세척시간=%lds alarm=%d\n", g_fieldDrop, get_process(a).Rewrite,
             (long)dur_s(a, SECTOR2_WASHING_START, SECTOR3_WASHING_END), rtc.HasAlarm(1));
        CHECK(g_fieldDrop >= 1, "①전제: 리셋으로 카드가 전원을 잃었다");
        const int32_t wd = dur_s(a, SECTOR2_WASHING_START, SECTOR3_WASHING_END);
        CHECK(wd >= 4L * 60 && wd < 5L * 60 && rtc.HasAlarm(1),
              "①재초기화 뒤에도 올려 둔 태그를 3초짜리 종료로 재처리하지 않는다(자동종료 4분 유지)");
    }

    // ── ② 지원 안 되는 카드를 반드시 정지(HALT)시킨다(B P2-2) ──
    //    안 그러면 그 카드가 두 폴링마다 응답해 present 가 굳고, 위에 올린 스코프가 KeepAlive 로 먹힌다.
    {
        card_init_foreign(b, 0x09);
        b.sak = 0x00;                                    // MIFARE 1K/4K 가 아닌 카드 → Poll 이 Invalid 를 낸다
        card_place(&b);
        run_loops(1);                                    // 한 번 Poll
        tlog("  ②비지원 카드 뒤 카드상태=%d (HALT=%d)\n", (int)g_cardState, (int)CARD_HALT);
        CHECK(g_cardState == CARD_HALT, "②지원 안 되는 카드는 정지시킨다(present 가 굳지 않게)");
        card_remove(); run_loops(4);
    }

    // ── ⑥ 세척 자동 종료는 커밋 앞 — 자동 종료 기록 블록이 실패하면 시작이 커밋되지 않는다(A P2-1) ──
    //    ★블록만 실패(카드는 살아 있음)로 순서를 가른다: 커밋 뒤에 두면(옛 결함) 커밋은 이미 됐고 성공음이 난다.
    {
        fresh_scope(a, 0x23, 23);
        a.nackBlock = SECTOR3_WASHING_END;               // 자동 종료 기록 블록만 실패
        logs_clear(); buzz_clear();
        touch(a, 1, 4);
        tlog("  ⑥자동종료 실패: RW=%u 성공음=%u Write Error=%d\n", get_process(a).Rewrite, buzz_count(50), lcd_has("Write Error"));
        CHECK(get_process(a).Rewrite == 0 && buzz_count(50) == 0,
              "⑥세척 자동 종료 실패 → 커밋 안 됨(RW=0) + 성공음 없음");
    }

    // ── ⑧ 종료 기록은 시각을 마지막에 — 담당자 블록만 실패하면 종료 시각이 새것으로 바뀌지 않는다(H P2-H1) ──
    {
        fresh_scope(a, 0x24, 24);
        touch(a);                                        // 정상 시작(자동 종료 = 시작+4분)
        const LocalDateTime autoEnd = get_ldt(a, SECTOR3_WASHING_END);
        sim_advance_ms(60UL * 1000);                     // 실제 종료는 시작+1분(새 시각)
        a.nackBlock = SECTOR4_WASHING_END_MANAGER_KEY;   // 종료 담당자 블록만 실패(카드는 산다)
        touch(a);
        const LocalDateTime after = get_ldt(a, SECTOR3_WASHING_END);
        tlog("  ⑧종료 담당자 실패: 종료시각 분 %u → %u (autoEnd 유지여야)\n", autoEnd.Time.Minute, after.Time.Minute);
        CHECK(after.Time.Minute == autoEnd.Time.Minute,
              "⑧담당자 블록 실패 → 종료 시각은 새것으로 바뀌지 않는다(옛 담당자+새 시각 쌍 금지)");
    }

    // ── ⑦ 소독 자동 종료가 커밋 앞 · ⑨ 이동은 종료 기록 뒤에 커밋 ──
    reboot_as('D');
    touch(mgr);
    {
        washed_scope(a, 0x25, 25);
        a.nackBlock = SECTOR6_DISINFECTION_END;          // 자동 종료 기록 블록만 실패(카드는 산다)
        logs_clear(); buzz_clear();
        touch(a, 1, 4);
        tlog("  ⑦소독 시작 중 실패: RW=%u 성공음=%u\n", get_process(a).Rewrite, buzz_count(50));
        CHECK(get_process(a).Rewrite != 2 && buzz_count(50) == 0, "⑦소독 자동 종료 실패 → 커밋 없음(RW≠2) + 성공음 없음");
    }
    {
        static SimCard clr;   // 이동 플래그용
        washed_scope(b, 0x26, 26);
        touch(b);                                        // 소독 시작
        sim_advance_ms(20UL * 60 * 1000);
        make_tag(clr, 0x27, CLEAR_TYPE_TAG, 0, "", "");
        touch(clr);                                      // 이동 플래그
        b.nackBlock = SECTOR6_DISINFECTION_END;          // 첫 이동은 isMoved=false → 종료를 SECTOR6 에 쓴다
        logs_clear(); buzz_clear();
        touch(b);
        const Process p = get_process(b);
        tlog("  ⑨이동 실패: MV=%u RW=%u\n", p.MovementNeeded, p.Rewrite);
        CHECK(p.MovementNeeded == 0 && p.Rewrite == 2, "⑨이동 종료 기록 실패 → '이동함' 커밋 안 됨(재접촉 복구 가능)");
    }

    // ── ③ 게이트웨이: 환자 구간 없는 패킷은 형제도 비운다 · ④ 여러 패킷이면 마지막을 쓴다 ──
    reboot_as('G');
    {
        const char p1[] = "G10000;G22026;9;25;5;10;0;0;G3OLD01;\xC8\xAB\xB1\xE6\xB5\xBF;G4EGD;;;G5;";
        serial_inject(p1, sizeof(p1) - 1); GUARDED(serialEvent()); run_loops(2);
        const char p2[] = "G10000;G22026;9;25;5;11;0;0;G5;";      // G3·G4 없음
        serial_inject(p2, sizeof(p2) - 1); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(a, 0x28, 28);
        logs_clear(); buzz_clear();
        touch(a);
        tlog("  ③G3 없는 패킷 뒤: 환자키=[%.5s] Status=%u NotPatient=%d\n",
             (const char *)a.data[SECTOR2_PATIENT_KEY], get_process(a).Status, serial_has("Not Patient Info"));
        CHECK(a.data[SECTOR2_PATIENT_KEY][0] == 0 && get_process(a).Status == 0 && serial_has("Not Patient Info"),
              "③환자 구간 없는 패킷 → 직전 환자가 남지 않고 폴백(환자정보 없음)으로 간다");

        // G2 필드 순서: 년;월;일;<무시>;시;분;초. 두 패킷의 '시' 를 12·13 으로 달리 둔다.
        const char p3[] = "G10000;G22026;9;25;5;12;0;0;G3KEYB;B;G4EGD;;;G5;"
                          "G10000;G22026;9;25;5;13;0;0;G3KEYC;C;G4COL;;;G5;";   // 두 패킷이 한 버퍼에
        serial_inject(p3, sizeof(p3) - 1); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(b, 0x29, 29);
        touch(b);
        const LocalDateTime gd = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  ④두 패킷: 환자키=[%.4s] 검사항목=[%.3s] 시=%u\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             (const char *)b.data[SECTOR15_EXAMINATION_SUBJECT], gd.Time.Hour);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "KEYC", 4) == 0 && gd.Time.Hour == 13 &&
              memcmp(b.data[SECTOR15_EXAMINATION_SUBJECT], "COL", 3) == 0,
              "④한 버퍼에 패킷 둘 → 마지막(최근) 환자·검사일시·검사항목을 쓴다");

        // ④-b 완전 패킷 뒤에 **잘린** 후행 패킷(G3 전 중단)이 오면 — 섞지 말고 폴백한다(재추적 검토 A/C).
        const char p4[] = "G10000;G22026;9;25;5;10;0;0;G3AAA111;\xC8\xAB;G4EGD;;;G5;"   // 완전(환자 AAA111·EGD)
                          "G10000;G22026;9;25;5;14;0;0;";                                 // 잘림(G3 없음)
        serial_inject(p4, sizeof(p4) - 1); GUARDED(serialEvent()); run_loops(2);
        static SimCard d;
        fresh_scope(d, 0x2B, 31);
        logs_clear();
        touch(d);
        tlog("  ④-b 완전+잘림: 환자키=[%.6s] 검사항목=[%.3s] NotPatient=%d\n",
             (const char *)d.data[SECTOR2_PATIENT_KEY], (const char *)d.data[SECTOR15_EXAMINATION_SUBJECT],
             serial_has("Not Patient Info"));
        CHECK(d.data[SECTOR2_PATIENT_KEY][0] == 0 && serial_has("Not Patient Info"),
              "④-b 마지막 레코드가 잘리면 앞 완전 레코드의 조각을 섞지 않고 폴백한다");

        // ④-c 등록키 값에 마커 문자열('G3')이 들어 있어도 앞의 마커를 집는다(첫 일치).
        const char p5[] = "G10000;G22026;9;25;5;11;0;0;G3G3PAT;\xB0\xA1;G4EGD;;;G5;";
        serial_inject(p5, sizeof(p5) - 1); GUARDED(serialEvent()); run_loops(2);
        static SimCard e;
        fresh_scope(e, 0x2C, 32);
        touch(e);
        tlog("  ④-c 값에 G3: 환자키=[%.6s]\n", (const char *)e.data[SECTOR2_PATIENT_KEY]);
        CHECK(memcmp(e.data[SECTOR2_PATIENT_KEY], "G3PAT", 5) == 0,
              "④-c 값에 'G3' 이 있어도 마커를 첫 일치로 집는다(잘리지 않음)");
    }

    // ── ⑩ 레거시 발급: 소거 실패는 성공이 아니다 ──
    reboot_as('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        fresh_scope(c, 0x2A, 30);
        set_process(c, Process{1, 1, 1, 1, 1, false, 0, 2});            // 지난 완료 상태
        memcpy(c.data[SECTOR2_PATIENT_KEY], "OLDPT", 6);
        c.nackBlock = SECTOR1_PROCESS;                                  // 발급의 Process 소거만 실패(카드는 산다)
        logs_clear();
        const char cmd[] = "S99;SER99;";                                // 번호;시리얼; — 두 세미콜론(파싱 성공)
        serial_inject(cmd, sizeof(cmd) - 1);
        card_place(&c);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  ⑩발급 소거 실패: lcd=[%.40s] WS=%u 환자=[%.5s]\n", g_lcdLog, get_process(c).WashingStatus,
             (const char *)c.data[SECTOR2_PATIENT_KEY]);
        CHECK(!lcd_has("new tag"), "⑩발급 중 소거 실패 → 'new tag' 로 안내하지 않는다");
    }

    // ── ⑪ 설정 JSON: 알 수 없는 타입은 매번 재시작하지 않는다 · ⑫ 없는 키는 건드리지 않는다 ──
    reboot_as('S');
    {
        alarmOption.SetTimeSlot1(30);
        disinfectionOption.SetMaximumCount(77);
        const uint16_t resets0 = g_resetCount;
        json("{\"cmd\":\"cfg_set_config\",\"device_type\":\"s\",\"device_number\":3}");   // 소문자 = 알 수 없는 타입
        const uint16_t r1 = g_resetCount;
        json("{\"cmd\":\"cfg_set_config\",\"device_type\":\"s\",\"device_number\":3}");   // 같은 것 재전송
        tlog("  ⑪알 수 없는 타입: type=%c no=%d resets %u→%u→%u\n", deviceOption.GetType(),
             deviceOption.GetNumber(), resets0, r1, g_resetCount);
        CHECK(g_resetCount == r1, "⑪알 수 없는 타입을 다시 보내도 재시작하지 않는다(저장값으로 판정)");
        CHECK(alarmOption.GetTimeSlot1() == 30 && disinfectionOption.GetMaximumCount() == 77,
              "⑫설정 일부만 보내면 나머지(세척시간·최대횟수)는 그대로다");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
