// 5차 전체 로직 추적 감사(09-27) 잠금 — 각 수정을 되돌리면 여기서 빨강이 나야 한다.
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

    // ═══ B P2-1: 비지원 카드 뒤 곧바로 스코프 — 첫 Poll 이 Connected 여야 한다(present 잔존이면 KeepAlive 로 무음) ═══
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND014567890123", "KIMJH");
    touch(mgr);
    {
        card_init_foreign(b, 0x09);
        b.sak = 0x00;                                    // 비 MIFARE 1K/4K → Invalid
        card_place(&b);
        run_loops(1);
        fresh_scope(a, 0x21, 21);
        card_place(&a);                                  // 치우지 않고 바로 교체 = 같이 있다가 스코프가 잡힘
        run_loops(3);
        card_remove(); run_loops(4);
        tlog("  B1 비지원 뒤 스코프: RW=%u\n", get_process(a).Rewrite);
        CHECK(get_process(a).Rewrite == 1, "B P2-1 비지원 카드 뒤 바로 잡힌 스코프가 처리된다(KeepAlive 로 먹히지 않음)");
    }

    // ═══ A1 P2-1: 세척기에 클리어 태그 → 거부음(무음 아님) ═══
    {

        make_tag(c, 0x02, CLEAR_TYPE_TAG, 0, "", "");
        buzz_clear();
        touch(c);
        tlog("  A1 세척기+클리어: 600=%u 60=%u\n", buzz_count(600), buzz_count(60));
        CHECK(buzz_count(600) == 1 && buzz_count(60) == 2, "A1 P2-1 세척기에 처리 못 하는 태그 → 거부음(삐삐+긴삐)");
    }

    // ═══ C P2-1: 일회성 담당자는 커밋된 시작에만 소모 ═══
    {
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                      // 일회성 충전
        fresh_scope(a, 0x22, 22);
        a.nackBlock = SECTOR2_WASHING_START;             // 시작 기록만 실패 → Write Error
        logs_clear();
        touch(a);
        CHECK(lcd_has("Write Error"), "C 전제: 시작 실패 → Write Error");
        a.nackBlock = -1;
        logs_clear(); buzz_clear();
        touch(a);                                        // 재접촉
        tlog("  C P2-1 재접촉: RW=%u NoManager=%d\n", get_process(a).Rewrite, lcd_has("No Manager Info"));
        CHECK(get_process(a).Rewrite == 1 && !lcd_has("No Manager Info"),
              "C P2-1 실패한 시작은 일회성 담당자를 소모하지 않아 재접촉이 정상 시작된다");
        recordOption.SetManagerDisposability(false);
    }

    // ═══ A2 P2-2: 붙은 버튼은 시한을 미루지 않는다(에지에서만 활동) ═══
    {
        g_btnIdleLimit = 2000000UL;
        buttons_script("S");
        const unsigned long t0 = millis();
        buttons_hold('L', t0 + 75000UL);                 // 75초 동안 L 이 눌린 채
        const int r = GUARDED(loop());
        const unsigned long dt = millis() - t0;
        tlog("  A2 P2-2 붙은 L: r=%d 경과=%lums\n", r, dt);
        CHECK(r == 0 && dt < 70000UL, "A2 P2-2 버튼이 붙어 있어도 첫 누름부터 60초에 메뉴를 나온다");
        buttons_hold(0, 0);
    }

    // ═══ A2/E1: 날짜 저장은 '지금' 의 시·분·초 (진입 스냅샷 아님) ═══
    {
        rtc_set(rel_date(10, 0, 0));
        buttons_script("L");                             // 저장 위치로
        const unsigned long t0 = millis();
        buttons_at('S', t0 + 30000UL);                   // 30초 뒤 저장
        GUARDED(ui.SetDeviceDate(rtc));
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  E1 날짜 저장 뒤 시계: %02u:%02u:%02u\n", now.hour(), now.minute(), now.second());
        CHECK(now.second() >= 30 || now.minute() >= 1, "A2/E1 날짜 저장이 편집에 걸린 시간만큼 시계를 되돌리지 않는다");
    }

    // ═══ A2 select: 현재값부터 시작 — 고르지 않고 저장해도 값이 안 바뀐다 ═══
    {
        deviceOption.SetType('D');
        const uint16_t resets0 = g_resetCount;
        buttons_script("LS");                            // 그냥 저장
        GUARDED(ui.SetDeviceType(deviceOption));
        tlog("  A2 select: type=%c resets %u→%u\n", deviceOption.GetType(), resets0, g_resetCount);
        CHECK(deviceOption.GetType() == 'D' && g_resetCount == resets0,
              "A2 타입 선택은 현재값부터 — 고르지 않고 저장하면 그대로(게이트웨이로 재시작 안 함)");
        recordOption.SetPatientCheck(false);
        buttons_script("LS");
        GUARDED(ui.SetRecordPatientCheck(recordOption));
        CHECK(recordOption.GetPatientCheck() == false, "A2 Yes/No 선택도 현재값부터(No 가 Yes 로 안 바뀜)");
    }

    // ═══ A2 제목 버퍼: "Number (300)" 닫는 괄호 ═══
    {
        deviceOption.SetNumber(300);
        logs_clear();
        buttons_script("LS");
        GUARDED(ui.SetDeviceNumber(deviceOption));
        CHECK(lcd_has("Number (300)"), "A2 제목 버퍼 — 세 자리 번호에서 닫는 괄호가 잘리지 않는다");
        deviceOption.SetNumber(1);
    }

    // ═══ C P1-1: 슬롯 없는 스코프의 종료가 host 슬롯을 지우지 않는다 ═══
    reboot_as('D');
    touch(mgr);
    {
        disinfectionOption.SetSimultaneousDisinfectionSlot(2);
        disinfectionOption.SetSimultaneousDisinfectionDelay(5);
        rtc_set(rel_date(11, 0, 0));
        washed_scope(a, 0x23, 23); touch(a);            // A host
        sim_advance_ms(10UL * 60 * 1000);
        washed_scope(b, 0x24, 24); touch(b);            // 창 밖 → B host(A 슬롯 덮음)
        sim_advance_ms(60UL * 1000);
        touch(a);                                        // A 종료(슬롯 없음) — B 슬롯을 지우면 안 된다
        const int cnt = disinfectionOption.GetCount();
        sim_advance_ms(60UL * 1000);
        washed_scope(c, 0x25, 25); touch(c);            // B 창 안 → guest 여야
        tlog("  C P1-1: 횟수 %d → %d\n", cnt, disinfectionOption.GetCount());
        CHECK(disinfectionOption.GetCount() == cnt, "C P1-1 host 창 안의 다음 스코프는 guest(횟수 불변) — 다른 스코프 종료가 슬롯을 안 지움");
        disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    }

    // ═══ C ⑤: 손상 시작 기록(연도 0xFFFF)이 RTC 를 바꾸지 않는다 ═══
    {
        rtc_set(rel_date(12, 0, 0));
        washed_scope(a, 0x26, 26);
        memset(a.data[SECTOR2_WASHING_START], 0xFF, 10);
        touch(a);
        tlog("  C5 손상 시작기록 뒤 RTC 년=%u\n", rtc.GetCurrentDateTime().year());
        CHECK(rtc.GetCurrentDateTime().year() == TRACEQ_RELEASE_YEAR, "C ⑤ 손상 시작 기록으로 RTC 를 2047 로 만들지 않는다");
    }

    // ═══ A1 P3-2 알람 줄 20자: 120분 설정 ═══
    {
        alarmOption.SetTimeSlot2(120);
        rtc_set(rel_date(13, 0, 0));
        washed_scope(b, 0x27, 27); touch(b);
        ui.InvalidateHome(); logs_clear(); run_loops(1);
        tlog("  A1 알람줄: [%.44s]\n", g_lcdLog);
        // 옛 버퍼[20] 은 마지막 글자를 잘라 "119:5"/"120:0" 까지만 — 초 두 자리가 온전해야 한다.
        CHECK(lcd_has("Alarm 119:59") || lcd_has("Alarm 120:00") || lcd_has("Alarm 119:58"),
              "A1 P3-2 알람 남은 시간(세 자리 분)의 초 두 자리가 잘리지 않는다");
        alarmOption.SetTimeSlot2(18);
    }

    // ═══ E4: 알람 슬롯은 1·2 뿐 ═══
    {
        rtc.ClearAlarm(2);
        rtc.SetAlarm(3, 5, 0);
        CHECK(!rtc.HasAlarm(2), "E4 슬롯 3 은 거부(슬롯 2 로 새지 않음)");
    }

    // ═══ E6: 교환일 '빔' 판정은 필드로 ═══
    {
        EEPROM.put(169, static_cast<uint16_t>(65535)); EEPROM.put(171, static_cast<uint8_t>(1)); EEPROM.put(172, static_cast<uint8_t>(0));
        CHECK(!disinfectionOption.IsClearDateTimeEmpty(), "E6 합이 0 으로 도는 손상값을 '빔' 으로 오판하지 않는다");
        disinfectionOption.SetClearDateTime(LocalDateTime{LocalDate{2026, 8, 1}, LocalTime{0, 0, 0}});
    }

    // ═══ E2/E5: 문자열 경계 ═══
    {
        char d[8] = "xxxxxxx";
        str_substring_safe("abcdef", d, sizeof(d), 4, 2);
        CHECK(d[0] == 0, "E2 뒤집힌 범위는 빈 문자열(스왑 안 함)");
        CHECK(str_atoi_range("12", 0, 255) == 12, "E5 end=255 에서 멈추지 않고 값을 돌려준다");
    }

    // ═══ E7: 유지 업그레이드 + 교환일 비어 있음 → 1개월 전 ═══
    {
        deviceOption.SetType('D');
        for (int i = 169; i <= 177; ++i) EEPROM.update(i, 0);   // 교환일·미룸 비움
        EEPROM.put((int)4088, (uint32_t)0);              // 새 펌웨어 업로드 흉내
        rtc_set(rel_date(10, 0, 0));
        hard_reset(false, 0);                            // 무응답 → 유지
        const LocalDateTime cd = disinfectionOption.GetClearDateTime();
        const LocalDateTime exp = DisinfectionOption::OneMonthBefore(DefaultRtc::ToLocalDateTime(rel_date(10, 0, 0)));
        tlog("  E7 유지 뒤 교환일 %u-%u-%u (기대 %u-%u-%u)\n", cd.Date.Year, cd.Date.Month, cd.Date.Day, exp.Date.Year, exp.Date.Month, exp.Date.Day);
        CHECK(cd.Date.Year == exp.Date.Year && cd.Date.Month == exp.Date.Month && cd.Date.Day == exp.Date.Day,
              "E7 유지 업그레이드에서 교환일이 비어 있으면 1개월 전(사장님 09-27)");
    }

    // ═══ D: 게이트웨이 — 값 속 마커 · G2 범위 · Status=1 재등록 ═══
    reboot_as('G');
    rtc_set(rel_date(10, 0, 0));
    {
        char p[120];
        snprintf(p, sizeof(p), "G10000;G2%u;%u;%u;5;10;0;0;G3G1234;KIG4M;G4EG5D;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(p, strlen(p)); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(a, 0x31, 31);
        touch(a);
        tlog("  D P2-1 값 속 마커: 키=[%.5s] 이름=[%.5s] 검사명=[%.5s] Status=%u\n", (const char *)a.data[SECTOR2_PATIENT_KEY],
             (const char *)a.data[SECTOR2_PATIENT_NAME], (const char *)a.data[SECTOR15_EXAMINATION_SUBJECT], get_process(a).Status);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "G1234", 5) == 0 && memcmp(a.data[SECTOR2_PATIENT_NAME], "KIG4M", 5) == 0 &&
              memcmp(a.data[SECTOR15_EXAMINATION_SUBJECT], "EG5D", 4) == 0 && get_process(a).Status == 1,
              "D P2-1 값 안의 G1/G4/G5 를 마커로 집지 않는다(키·이름·검사명 온전)");

        // 세척관리 형식(';' 없이 G5 로 끝) 패킷 둘이 한 버퍼 → 마지막(최근) 환자
        char two[160];
        snprintf(two, sizeof(two), "G1G2%u;%u;%u;5;10;0;0;G3OLD01;A;G4EGD;;;G5G1G2%u;%u;%u;5;11;0;0;G3NEW01;B;G4COL;;;G5",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY,
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(two, strlen(two)); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(c, 0x39, 39);
        touch(c);
        tlog("  D G5G1 인접: 키=[%.5s]\n", (const char *)c.data[SECTOR2_PATIENT_KEY]);
        CHECK(memcmp(c.data[SECTOR2_PATIENT_KEY], "NEW01", 5) == 0, "D 세척관리 패킷 둘(G5G1 인접) → 마지막 환자");

        const char q[] = "G10000;G22026;13;40;5;10;0;0;G3PT9;KIM;G4EGD;;;G5;";   // 월 13
        serial_inject(q, sizeof(q) - 1); GUARDED(serialEvent()); run_loops(2);
        fresh_scope(b, 0x32, 32);
        touch(b);
        tlog("  D G2 범위: RTC 년=%u 검사일시 년=%u\n", rtc.GetCurrentDateTime().year(), get_ldt(b, SECTOR1_GATEWAY).Date.Year);
        CHECK(rtc.GetCurrentDateTime().year() == TRACEQ_RELEASE_YEAR && get_ldt(b, SECTOR1_GATEWAY).Date.Year == 0,
              "D G2 범위 밖(월 13)은 검사일시 없음 — RTC 를 바꾸지 않는다");

        // Status=1 이던 태그에 새 환자를 쓰다 이름 블록만 실패 → Status 가 1 로 남으면 안 된다
        char p2[96];
        snprintf(p2, sizeof(p2), "G10000;G2%u;%u;%u;5;11;0;0;G3NEW01;NEWNAME;G4EGD;;;G5;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        serial_inject(p2, strlen(p2)); GUARDED(serialEvent()); run_loops(2);
        a.nackBlock = SECTOR2_PATIENT_KEY;               // a 는 위에서 Status=1 이 됐다
        a.nackBlockSkip = 1;                             // 선내림 뒤 소거(첫 쓰기)는 통과, 새 키 쓰기만 실패
        logs_clear();
        touch(a);
        tlog("  D P2-3 재등록 이름 실패: Status=%u 키=[%.5s] 이름=[%.5s]\n", get_process(a).Status,
             (const char *)a.data[SECTOR2_PATIENT_KEY], (const char *)a.data[SECTOR2_PATIENT_NAME]);
        CHECK(get_process(a).Status == 0, "D P2-3 환자정보 있던 태그에 새 환자 쓰다 실패 → Status=1+혼합이 아니라 Status=0");
        CHECK(a.data[SECTOR2_PATIENT_KEY][0] == 0 && a.data[SECTOR2_PATIENT_NAME][0] == 0,
              "D P2-3 실패한 재등록 뒤 옛 환자 키·이름이 블록에 남지 않는다(PC 는 Status 무관하게 블록을 쓴다)");
    }

    // ═══ D P2-2: 레거시 발급 번호 -1 금지 ═══
    reboot_as('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        fresh_scope(c, 0x33, 33);
        logs_clear();
        const char cmd[] = "S;SER01;";                   // 번호 빈값
        serial_inject(cmd, sizeof(cmd) - 1);
        card_place(&c);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  D P2-2 빈 번호 발급: lcd=[%.40s] 번호=%d\n", g_lcdLog, (int)*(int16_t *)c.data[SECTOR0_TAG]);
        CHECK(!lcd_has("new tag"), "D P2-2 번호 빈값·비숫자는 발급 실패(-1 태그를 만들지 않는다)");
    }

    // ═══ B P2-2: 반만 바뀐 태그(전원 상실)도 type 0 재발급이 통과한다 ═══
    {

        card_init_foreign(c, 0x40);                   // 공장 키(KEY_A FF) 카드
        for (uint8_t s = 0; s < 16; ++s)
        {
            memset(c.keyB[s], 0xFF, 6); c.keyBAuth[s] = false;
            c.data[s * 4 + 3][6] = 0xFF; c.data[s * 4 + 3][7] = 0x07; c.data[s * 4 + 3][8] = 0x80;   // 공장 운송 접근조건
        }
        // 앞 3섹터만 TraceQ 로 바뀐 상태(접근조건 011 + KEY_B = TraceQ 키)
        static const uint8_t kKey[6] = {0x90, 0x25, 0x84, 0x71, 0x84, 0x72};   // fake 의 TraceQ 키
        for (uint8_t s = 0; s < 3; ++s)
        {
            memcpy(c.keyB[s], kKey, 6); c.keyBAuth[s] = true;
            c.data[s * 4 + 3][6] = 0x0F; c.data[s * 4 + 3][7] = 0x00; c.data[s * 4 + 3][8] = 0xFF;   // 접근조건 011 바이트
        }
        logs_clear();
        const char nt[] = "{\"cmd\":\"cfg_new_tag\",\"type_id\":0}";
        serial_inject(nt, sizeof(nt) - 1);
        card_place(&c);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  B P2-2 반만 바뀐 태그 재발급: lcd=[%.40s]\n", g_lcdLog);
        CHECK(lcd_has("tag created"), "B P2-2 발급 도중 전원이 나간 태그도 type 0 재발급이 통과한다(멱등)");
    }

    // ═══ P3 마무리 잠금 ═══
    // 미인증 서버 + 스코프 → 거부음(무음 아님)
    reboot_as('S');
    {
        fresh_scope(a, 0x51, 51);
        set_process(a, Process{1, 1, 1, 1, 1, false, 0, 2});
        logs_clear(); buzz_clear();
        touch(a);                                        // 'Z' 없이
        tlog("  P3 미인증 서버: 600=%u Ok!=%d\n", buzz_count(600), serial_has("Ok!"));
        CHECK(buzz_count(600) == 1 && !serial_has("Ok!"), "P3 미인증 서버에 스코프 → 거부음(덤프는 없음)");
    }
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        // device_type "" 은 무시(W 로 바뀌며 재시작하던 것)
        deviceOption.SetType('S');
        const uint16_t r0 = g_resetCount;
        json("{\"cmd\":\"cfg_set_config\",\"device_type\":\"\"}");
        tlog("  P3 device_type '': type=%c resets %u→%u\n", deviceOption.GetType(), r0, g_resetCount);
        CHECK(deviceOption.GetType() == 'S' && g_resetCount == r0, "P3 device_type \"\" 은 무시 — 타입 유지·재시작 없음");
        // 파싱 실패 발급 명령은 4초를 기다리지 않는다
        const unsigned long t0 = millis();
        logs_clear();
        serial_inject("S99;", 4);                       // 세미콜론 하나 = 파싱 실패
        GUARDED(serialEvent());
        tlog("  P3 파싱 실패 발급: 경과=%lums lcd=[%.30s]\n", millis() - t0, g_lcdLog);
        CHECK(millis() - t0 < 2000UL && lcd_has("timeout or error"), "P3 파싱 실패 발급은 즉시 실패(4초 대기 없음)");
        // ClearSector 범위
        CHECK(rfid.ClearSector(16) == RfidResult::InvalidArgument, "P3 ClearSector(16) 은 거부");
    }
    // 최대 횟수 뒤에도 환자정보 경고가 난다
    reboot_as('D');
    touch(mgr);
    {
        recordOption.SetPatientCheck(true);
        disinfectionOption.SetMaximumCount(1);
        disinfectionOption.SetCount(5);
        washed_scope(b, 0x52, 52);
        set_process(b, Process{0, 0, 1, 0, 1, false, 0, 0});   // 환자정보 없음
        logs_clear(); buzz_clear();
        touch(b);
        tlog("  P3 MaxCount+환자없음: MaxCount=%d NoPatient=%d\n", lcd_has("MaxCount Over"), lcd_has("No Patient Info"));
        CHECK(lcd_has("MaxCount Over") && lcd_has("No Patient Info"), "P3 최대 횟수 안내가 환자정보 경고를 가리지 않는다");
        recordOption.SetPatientCheck(false);
        disinfectionOption.SetMaximumCount(0);
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
