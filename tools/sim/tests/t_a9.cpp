// Y2·Z1 잠금 — 연속된 EEPROM 쓰기 사이에 전원이 나가도 남는 상태가 안전한가(찢긴 값이 기록에 실리지 않는가).
// 장치: fake_hw 의 전원 차단 주입(g_eepromCutAfter = n → n번째 바이트 쓰기까지만 남고 그 뒤는 사라짐) → hard_reset.
// 절단점 n 을 0..K 전부 훑는다(K = 차단 없는 같은 조작의 총 쓰기 수). 되돌리면 여기서 빨강이 나야 한다.
#include "common.h"

static SimCard mgr, mgrB, clr, sc;

static LocalDateTime ldt(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
{
    return LocalDateTime{LocalDate{y, mo, d}, LocalTime{h, mi, s}};
}
static bool ldt_same(const LocalDateTime &a, const LocalDateTime &b)
{
    return ldt_eq(a, b.Date.Year, b.Date.Month, b.Date.Day, b.Time.Hour, b.Time.Minute, b.Time.Second);
}
static uint32_t ldt_secs(const LocalDateTime &t)
{
    return DateTime(t.Date.Year, t.Date.Month, t.Date.Day, t.Time.Hour, t.Time.Minute, t.Time.Second).unixtime();
}
static void reboot() { power_restore(); hard_reset(false, 2); }
static void as_type(char t) { power_restore(); deviceOption.SetType(t); hard_reset(false, 2); }
static void fresh_scope(uint8_t uid)
{
    make_tag(sc, uid, SCOPE_TYPE_TAG, 11, "SC0011", "S0011");
    set_process(sc, Process{0, 0, 1, 0, 1, false, 0, 1});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}
static bool block_zero(const SimCard &c, uint8_t blk)
{
    for (uint8_t i = 0; i < 16; ++i)
        if (c.data[blk][i] != 0) return false;
    return true;
}

static void bb_washed(uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(sc, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(8, 0, 0));   // 되돌린 시계보다 앞 — RTC 복구가 끼지 않게
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(8, 4, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    make_tag(mgrB, 0x02, MANAGER_TYPE_TAG, 8, "ND09999", "PARKSS");
    make_tag(clr, 0x03, CLEAR_TYPE_TAG, 0, "", "");
    touch(mgr);

    // ── 클리어 태그: 미룸을 맨 먼저 세운다(Y2 P3-1 섞인 날짜 · Z1 P3-1 사라진 액교환) ──
    {
        const LocalDateTime oldClear = ldt(2026, 8, 15, 9, 0, 0);
        auto pre = [&]() {
            power_restore();
            rtc_set(rel_date(10, 0, 0));
            disinfectionOption.SetCount(5);
            disinfectionOption.SetMaximumCount(4);           // 이미 상한 넘김 → MaxCount Over 가 떠 있어야 한다
            disinfectionOption.SetClearDateTime(oldClear);
            disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            disinfectionOption.SetClearCount(3);
        };
        pre();
        g_eepromWrites = 0;
        touch(clr);
        const uint32_t K = g_eepromWrites;
        const LocalDateTime newClear = disinfectionOption.GetClearDateTime();
        tlog("  클리어 터치 쓰기 수 K=%lu\n", (unsigned long)K);
        CHECK(disinfectionOption.GetCount() == 0 && disinfectionOption.GetClearCount() == 4 && K >= 4 &&
              ldt_same(newClear, ldt(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY, 10, 0, 0)),
              "대조: 차단 없는 클리어 = 횟수 0·클리어횟수 4·교환일 10:00");

        uint8_t mixed = 0, lost = 0;
        for (uint32_t n = 0; n <= K; ++n)
        {
            pre();
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            touch(clr);
            power_restore();
            sim_advance_ms(3UL * 3600 * 1000);               // 3시간 꺼져 있다가 다시 켠다
            const uint32_t t0 = ldt_secs(rtc.GetCurrentLocalDateTime());
            hard_reset(false, 2);
            const uint32_t t1 = ldt_secs(rtc.GetCurrentLocalDateTime());
            const LocalDateTime d = disinfectionOption.GetClearDateTime();
            const uint32_t ds = ldt_secs(d);
            // 남을 수 있는 값은 셋뿐: 옛 교환일 · 새 교환일 · (미룸 복구로) 재개 무렵. 그 밖은 찢긴 날짜다.
            const bool known = ldt_same(d, oldClear) || ldt_same(d, newClear) || (ds >= t0 && ds <= t1);
            // 횟수만 0 이 되고 교환일은 지난달 = 액교환이 기록에서 사라진 상태(MaxCount Over 도 안 뜬다)
            const bool gone = (disinfectionOption.GetCount() == 0 && ldt_same(d, oldClear));
            if (!known) ++mixed;
            if (gone) ++lost;
            if (!known || gone)
            tlog("  클리어 n=%lu: %04u-%02u-%02u %02u:%02u:%02u cnt=%d ccnt=%d pend=%u%s%s\n", (unsigned long)n,
                 d.Date.Year, d.Date.Month, d.Date.Day, d.Time.Hour, d.Time.Minute, d.Time.Second,
                 disinfectionOption.GetCount(), disinfectionOption.GetClearCount(),
                 disinfectionOption.GetClearPending(), known ? "" : " ★섞인 날짜", gone ? " ★액교환 사라짐" : "");
        }
        tlog("  클리어 절단점 %lu: 섞인 날짜 %u · 액교환 사라짐 %u\n", (unsigned long)(K + 1), mixed, lost);
        CHECK(mixed == 0, "Y2 P3-1 클리어: 어느 절단점에서 끊겨도 교환일은 옛 값·새 값·재개 무렵 중 하나(섞인 날짜 없음)");
        CHECK(lost == 0, "Z1 P3-1 클리어: '횟수 0 + 지난달 교환일'(액교환이 기록에서 사라진 상태)로 남는 절단점 없음");

        // 교환일 도중 절단 → 미룸이 남아 다음 부팅이 다시 쓴다(재개 무렵) · 미룸이 떠돌지 않는다
        pre();
        g_eepromWrites = 0; g_eepromCutAfter = 4;            // 교환일 바이트 도중
        touch(clr);
        power_restore();
        sim_advance_ms(3UL * 3600 * 1000);
        const uint32_t r0 = ldt_secs(rtc.GetCurrentLocalDateTime());
        hard_reset(false, 2);
        const uint32_t r1 = ldt_secs(rtc.GetCurrentLocalDateTime());
        const LocalDateTime rec = disinfectionOption.GetClearDateTime();
        tlog("  교환일 도중 절단 → %04u-%02u-%02u %02u:%02u pend=%u\n", rec.Date.Year, rec.Date.Month, rec.Date.Day,
             rec.Time.Hour, rec.Time.Minute, disinfectionOption.GetClearPending());
        CHECK(ldt_secs(rec) >= r0 && ldt_secs(rec) <= r1 && !disinfectionOption.HasPendingClear(),
              "Y2 P3-1 교환일 도중 절단은 미룸으로 복구된다(재개 무렵 · 미룸이 남아 떠돌지 않는다)");

        // 찢긴 날짜가 소독 기록(태그 상세 56)에 실리지 않는다
        fresh_scope(0x11);
        touch(sc);
        LocalDateTime det{};
        memcpy(&det, sc.data[SECTOR14_DISINFECTION_DETAIL], sizeof(det));
        tlog_ldt("절단 뒤 첫 소독 태그 상세 교환일", det);
        CHECK(ldt_same(det, disinfectionOption.GetClearDateTime()) && (ldt_secs(det) >= r0 && ldt_secs(det) <= r1),
              "Y2 P3-1 태그 상세(56)에 실리는 교환일도 복구된 값(섞인 날짜가 아니다)");
        disinfectionOption.SetMaximumCount(0);
    }

    // ── 담당자 등록: 표지를 먼저 내리고 바이트를 다 넣은 뒤 한 번 올린다(Y2 P3-2) ──
    //    표지가 거짓이면 칸에 반쪽이 남을 수 있으니 읽는 쪽이 표지를 본다(Z1 P3-2).
    uint32_t mgrK = 0;
    {
        unsigned char kA[16]{}, kB[16]{}, nA[16]{}, nB[16]{};
        memcpy(kA, mgr.data[SECTOR0_TAG] + 2, 14);
        memcpy(kB, mgrB.data[SECTOR0_TAG] + 2, 14);
        memcpy(nA, mgr.data[SECTOR1_TAG_SERIAL], 16);
        memcpy(nB, mgrB.data[SECTOR1_TAG_SERIAL], 16);

        power_restore();
        touch(mgr);
        g_eepromWrites = 0;
        touch(mgrB);
        mgrK = g_eepromWrites;
        tlog("  담당자 A→B 쓰기 수 K=%lu\n", (unsigned long)mgrK);
        CHECK(mgrK >= 4 && managerOption.HasData(), "대조: A→B 등록은 키·이름 여러 바이트를 쓴다");

        // 같은 담당자를 다시 대면 한 바이트도 쓰지 않는다(표지 마모)
        g_eepromWrites = 0;
        touch(mgrB);
        tlog("  담당자 B→B 재접촉 쓰기 수 = %lu\n", (unsigned long)g_eepromWrites);
        CHECK(g_eepromWrites == 0 && managerOption.HasData(),
              "Z1 P3-4 같은 담당자를 다시 대면 EEPROM 을 한 바이트도 쓰지 않는다(표지 마모 없음)");

        uint8_t mixed = 0;
        for (uint32_t n = 0; n <= mgrK; ++n)
        {
            power_restore();
            touch(mgr);
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            touch(mgrB);
            reboot();
            unsigned char k[16]{}, nm[16]{};
            managerOption.GetKey(k, 14);
            managerOption.GetName(nm, 16);
            const bool isA = memcmp(k, kA, 14) == 0 && memcmp(nm, nA, 16) == 0;
            const bool isB = memcmp(k, kB, 14) == 0 && memcmp(nm, nB, 16) == 0;
            const bool has = managerOption.HasData();
            if (has && !isA && !isB) ++mixed;
            if (has && !isA && !isB)
            tlog("  담당자 n=%lu: has=%d key=%.14s name=%.16s ★반쪽 담당자\n", (unsigned long)n, has,
                 (const char *)k, (const char *)nm);
        }
        tlog("  담당자 반쪽 절단점 %u / %lu\n", mixed, (unsigned long)(mgrK + 1));
        CHECK(mixed == 0, "Y2 P3-2 담당자 등록: '담당자 있음' 으로 남은 절단점의 쌍은 정확히 A 이거나 B");

        // ★반쪽이 남은 상태(표지 거짓 + 칸에 섞인 키·이름)에서 세척 종료를 대면 종료 담당자 블록(16/17)이 0 이어야 한다
        as_type('W');
        recordOption.SetManagerDisposability(true);           // 일회성 ON — 종료 터치는 표지를 안 보고 기록했다
        touch(mgr);                                           // 온전한 담당자로 세척 시작
        fresh_scope(0x12);
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        touch(sc);
        const bool started = !block_zero(sc, SECTOR3_WASHING_START_MANAGER_KEY);
        power_restore();
        touch(mgr);
        g_eepromWrites = 0; g_eepromCutAfter = (int32_t)(mgrK / 2);
        touch(mgrB);                                          // 등록 도중 전원 차단 → 반쪽
        reboot();
        recordOption.SetManagerDisposability(true);
        sim_advance_ms(20UL * 60 * 1000);
        logs_clear();
        touch(sc);                                            // 세척 종료
        const bool ended = !block_zero(sc, SECTOR3_WASHING_END);
        tlog("  반쪽 담당자로 세척 종료: 시작기록=%d 종료기록=%d has=%d 종료담당자키=%.14s\n", started, ended,
             managerOption.HasData(), (const char *)sc.data[SECTOR4_WASHING_END_MANAGER_KEY]);
        CHECK(started && ended, "전제: 세척 시작·종료가 실제로 기록됐다");
        CHECK(managerOption.HasData() ||
                  (block_zero(sc, SECTOR4_WASHING_END_MANAGER_KEY) && block_zero(sc, SECTOR4_WASHING_END_MANAGER_NAME)),
              "Z1 P3-2 표지가 거짓이면 종료 담당자 블록(16/17)은 0 — 반쪽 담당자를 기록하지 않는다");
        recordOption.SetManagerDisposability(false);
        as_type('D');
        touch(mgr);
    }

    // ── 같은 판 RIGHT 소거 중 전원 차단: 다음 부팅은 옛 설정 그대로 또는 깨끗한 기본값이어야 한다 ──
    //    (Y2 P3-4 도장 무효화 · Z1 P3-3 DeviceOption::Upload 는 타입을 마지막에)
    {
        power_restore();
        buttons_script("");
        for (int i = 0; i < EEPROM.length(); ++i) EEPROM.update(i, 0);
        hard_reset(false, 0);                                 // 도장 0 → 묻지 않고 기본값
        const char defType = deviceType;
        const int defNumber = deviceOption.GetNumber();
        const int defSlot2 = alarmOption.GetTimeSlot2();
        const int defMax = disinfectionOption.GetMaximumCount();
        tlog("  기본값: type=%c no=%d slot2=%d max=%d\n", defType, defNumber, defSlot2, defMax);

        auto pre = [&]() {
            power_restore();
            buttons_script("");
            for (int i = 0; i < EEPROM.length(); ++i) EEPROM.update(i, 0);
            hard_reset(false, 0);
            deviceOption.SetType('D');
            deviceOption.SetNumber(5);
            alarmOption.SetTimeSlot2(17);
            disinfectionOption.SetMaximumCount(31);
            EEPROM.put((int)4088, fw_stamp());
            touch(mgr);
        };
        pre();
        buttons_script("RRRRrRS");
        g_eepromWrites = 0;
        hard_reset(false, 0);
        const uint32_t K = g_eepromWrites;
        tlog("  RIGHT 소거 쓰기 수 K=%lu\n", (unsigned long)K);
        CHECK(K >= 20 && deviceType == defType && deviceOption.GetNumber() == defNumber,
              "대조: 차단 없는 RIGHT 소거 = 기본값");

        uint8_t bad = 0;
        for (uint32_t n = 0; n <= K; ++n)
        {
            pre();
            buttons_script("RRRRrRS");
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            hard_reset(false, 0);
            power_restore();
            buttons_script("");                               // 무응답(=유지) 로 다시 켠다
            logs_clear();
            hard_reset(false, 2);
            const bool oldKept = deviceType == 'D' && deviceOption.GetNumber() == 5 &&
                                 alarmOption.GetTimeSlot2() == 17 &&
                                 disinfectionOption.GetMaximumCount() == 31 && managerOption.HasData();
            const bool cleanDefault = deviceType == defType && deviceOption.GetNumber() == defNumber &&
                                      alarmOption.GetTimeSlot2() == defSlot2 &&
                                      disinfectionOption.GetMaximumCount() == defMax && !managerOption.HasData();
            if (!oldKept && !cleanDefault) ++bad;
            if (!oldKept && !cleanDefault)
            tlog("  소거 n=%lu %c type=%c no=%d slot2=%d max=%d mgr=%d\n", (unsigned long)n,
                 oldKept ? 'K' : cleanDefault ? 'C' : 'X', deviceType, deviceOption.GetNumber(),
                 alarmOption.GetTimeSlot2(), disinfectionOption.GetMaximumCount(), managerOption.HasData());
        }
        tlog("  소거 절단점 %lu 중 어중간하게 굳은 것 %u\n", (unsigned long)(K + 1), bad);
        CHECK(bad == 0, "Y2 P3-4 · Z1 P3-3 소거 중 끊겨도 다음 부팅은 옛 설정 그대로이거나 깨끗한 기본값");
    }

    // ── AA1 P3-4·P3-5: SetData 의 "바뀌는 것이 없으면 안 쓴다" 판정이 무엇을 보는가 ──
    {
        as_type('W');
        unsigned char kA[16]{}, nA[16]{}, nC[16]{};
        memcpy(kA, mgr.data[SECTOR0_TAG] + 2, 14);
        memcpy(nA, mgr.data[SECTOR1_TAG_SERIAL], 16);
        memcpy(nC, "OTHERNAME", 9);
        // (a) 표지만 찢긴 상태(키·이름은 맞고 표지 거짓)는 같은 담당자를 다시 대면 복구돼야 한다
        managerOption.SetData(kA, nA);
        EEPROM.put(65, false);                           // 표지만 거짓으로
        CHECK(!managerOption.HasData(), "AA1 전제: 표지만 거짓인 상태를 만들었다");
        managerOption.SetData(kA, nA);
        tlog("  AA1 P3-4 표지만 찢김 → 재등록 뒤 has=%d\n", managerOption.HasData());
        CHECK(managerOption.HasData(), "AA1 P3-4 표지만 찢긴 상태는 같은 담당자 재접촉으로 복구된다");
        // (b) 키가 같고 이름만 다르면 갱신돼야 한다
        managerOption.SetData(kA, nC);
        unsigned char got[16]{};
        managerOption.GetName(got, 16);
        tlog("  AA1 P3-5 이름만 다른 담당자 → 이름=[%.9s]\n", (const char *)got);
        CHECK(memcmp(got, nC, 9) == 0, "AA1 P3-5 키가 같고 이름만 달라도 갱신된다");
        managerOption.SetData(kA, nA);
        as_type('D');
        touch(mgr);
    }
    // ── AA1 P3-3: 유지(keep) 부팅에서 빈 교환일을 채우는 도중 끊겨도 쓸 수 없는 날짜로 굳지 않는다 ──
    //    되돌리면 절단점 몇 곳에서 교환일이 0234-00-00·2026-00-00 처럼 영구히 굳는다(스스로 못 낫는다).
    {
        uint8_t bad = 0;
        for (uint32_t n = 0; n <= 12; ++n)
        {
            power_restore();
            rtc_set(rel_date(10, 0, 0));
            // 쓰던 기기(도장 같음) + 교환일 칸이 비어 있는 상태를 만든다
            for (int i = 0; i < EEPROM.length(); ++i) EEPROM.update(i, 0);
            hard_reset(false, 0);
            deviceOption.SetType('D');
            LocalDateTime empty{};
            disinfectionOption.SetClearDateTime(empty);
            disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            EEPROM.put((int)4088, (uint32_t)0x12345678UL);   // ★옛 판 도장 — 이래야 초기화 블록에 들어간다
            buttons_script("");                          // 무응답 10초 = '유지' 선택
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            hard_reset(false, 0);                        // 유지 갈래: 빈 교환일을 '1개월 전' 으로 채운다
            power_restore();
            hard_reset(false, 2);                        // 다시 켜서 복구 기회를 준다
            const LocalDateTime d = disinfectionOption.GetClearDateTime();
            // 빈 칸(0000-00-00)은 한 번뿐인 이관을 놓친 것 — 클리어 태그로 낫는 종전 상태다.
            // 막아야 하는 것은 **찢긴 날짜**(0234-00-00·2026-00-00 처럼 IsClearDateTimeEmpty 가 거짓이라
            // 영원히 안 고쳐지고 기록에 실리는 값)다.
            const bool isEmpty = (d.Date.Year == 0 && d.Date.Month == 0 && d.Date.Day == 0);
            const bool usable = isEmpty || (d.Date.Year >= 2026 && d.Date.Month >= 1 && d.Date.Month <= 12 &&
                                          d.Date.Day >= 1 && d.Date.Day <= 31);
            if (!usable) ++bad;
            if (!usable)
                tlog("  AA1 P3-3 n=%lu ★찢긴 교환일 %04u-%02u-%02u pend=%u\n", (unsigned long)n, d.Date.Year,
                     d.Date.Month, d.Date.Day, disinfectionOption.GetClearPending());
        }
        tlog("  AA1 P3-3 절단점 13 중 못 쓸 교환일로 굳은 것 %u\n", bad);
        CHECK(bad == 0, "AA1 P3-3 유지 부팅의 빈 교환일 채우기가 끊겨도 찢긴 날짜로 굳지 않는다(빈 칸은 클리어 태그로 낫는다)");
        power_restore();
        rtc_set(rel_date(10, 0, 0));
    }

    // ── 레거시 시각 동기 T (사장님 판정 09-27 — PC 시각을 그대로 받는다) ──
    {
        power_restore();
        static const char kTypes[] = {'W', 'D', 'G', 'S'};
        for (uint8_t ti = 0; ti < sizeof(kTypes); ++ti)
        {
            const char t = kTypes[ti];
            as_type(t);
            rtc_set(rel_date(10, 0, 0));
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "T%u;%u;%u;3;14;30;5;", (unsigned)TRACEQ_RELEASE_YEAR,
                     (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
            logs_clear();
            serial_inject(cmd, strlen(cmd));
            GUARDED(serialEvent());
            run_loops(1);
            const DateTime n = rtc.GetCurrentDateTime();
            tlog("  T 동기 type=%c → %02u:%02u:%02u 안내=%d\n", t, n.hour(), n.minute(), n.second(),
                 lcd_has("updated"));
            CHECK(n.hour() == 14 && n.minute() == 30 && n.second() >= 5 && lcd_has("updated"),   // 초는 시뮬 시간만큼 흐른다
                  "T 시각 동기는 모든 타입에서 듣는다");
        }
        // 요일 칸은 두 PC 가 뜻이 다르다(0=일 / 1=일) — 무엇이 와도 결과가 같아야 한다
        as_type('W');
        DateTime got[2];
        for (uint8_t i = 0; i < 2; ++i)
        {
            rtc_set(rel_date(10, 0, 0));
            char cmd[48];
            snprintf(cmd, sizeof(cmd), "T%u;%u;%u;%u;9;15;0;", (unsigned)TRACEQ_RELEASE_YEAR,
                     (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY, i == 0 ? 0u : 7u);
            serial_inject(cmd, strlen(cmd));
            GUARDED(serialEvent());
            run_loops(1);
            got[i] = rtc.GetCurrentDateTime();
        }
        tlog("  T 요일 0 vs 7: %02u:%02u / %02u:%02u\n", got[0].hour(), got[0].minute(), got[1].hour(),
             got[1].minute());
        CHECK(got[0].hour() == 9 && got[0].minute() == 15 && got[1].hour() == 9 && got[1].minute() == 15,
              "T 요일 칸은 무시한다(세척관리 0=일 · 올눈 1=일 둘 다 같은 결과)");
        // 범위 밖·칸 부족은 저장하지 않고 알린다
        rtc_set(rel_date(10, 0, 0));
        const char bad1[] = "T2026;13;40;3;25;70;70;";
        logs_clear();
        serial_inject(bad1, sizeof(bad1) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep1 = rtc.GetCurrentDateTime();
        const char bad2[] = "T2026;9;27;3;14;";
        logs_clear();
        serial_inject(bad2, sizeof(bad2) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep2 = rtc.GetCurrentDateTime();
        // 월·일만 어긋난 표본 — 시·분·초 범위 검사로는 안 걸리는 자리(RTC 에 월 13 이 저장되던 구멍 2.2.1)
        const char bad3[] = "T2026;13;40;3;14;30;0;";
        logs_clear();
        serial_inject(bad3, sizeof(bad3) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep3 = rtc.GetCurrentDateTime();
        tlog("  T 범위 밖 뒤 %02u:%02u · 칸 부족 뒤 %02u:%02u · 월13일40 뒤 %u-%u %02u:%02u 안내=%d\n",
             keep1.hour(), keep1.minute(), keep2.hour(), keep2.minute(), keep3.month(), keep3.day(),
             keep3.hour(), keep3.minute(), lcd_has("Invalid DateTime"));
        CHECK(keep1.hour() == 10 && keep2.hour() == 10 && lcd_has("Invalid DateTime"),
              "T 범위 밖·칸 부족은 시계를 바꾸지 않고 알린다");
        CHECK(keep3.month() == TRACEQ_RELEASE_MONTH && keep3.day() == TRACEQ_RELEASE_DAY && keep3.hour() == 10,
              "T 월·일만 어긋난 값도 시계를 바꾸지 않는다(월 13 이 RTC 에 들어가던 구멍)");
        // 연도 경계 — RTC 는 2000 기준 오프셋이라 1999·2100 은 isValid 로는 안 걸릴 수 있다
        const char bad4[] = "T1999;9;27;3;14;30;0;";
        logs_clear();
        serial_inject(bad4, sizeof(bad4) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep4 = rtc.GetCurrentDateTime();
        const char bad5[] = "T2100;9;27;3;14;30;0;";
        serial_inject(bad5, sizeof(bad5) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep5 = rtc.GetCurrentDateTime();
        tlog("  T 연도 1999 뒤 %u-%02u %02u:%02u · 2100 뒤 %u %02u:%02u\n", keep4.year(), keep4.month(),
             keep4.hour(), keep4.minute(), keep5.year(), keep5.hour(), keep5.minute());
        CHECK(keep4.year() == TRACEQ_RELEASE_YEAR && keep4.hour() == 10 &&
              keep5.year() == TRACEQ_RELEASE_YEAR && keep5.hour() == 10,
              "T 연도가 2000~2099 밖이면 시계를 바꾸지 않는다");
        // ★비숫자 칸 — 범위 검사를 지우면 str_atoi_range 가 -1 을 주고 RTClib 이 **2047년**으로 저장한다
        //  (isValid 는 참이라 안 걸린다 · 스스로 낫지 않는다). 6차에 내가 "지워도 구별 못 한다" 고 잘못 적은 자리(BB1 P3-1).
        const char bad6[] = "TABCD;9;27;3;14;30;0;";
        logs_clear();
        serial_inject(bad6, sizeof(bad6) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep6 = rtc.GetCurrentDateTime();
        // 칸 경계가 255 를 넘는 전문도 조용히 잘못된 시계가 되지 않는다(BB1 P3-4)
        char longT[300];
        memset(longT, '9', sizeof(longT));
        longT[0] = 'T';
        longT[5] = ';';
        longT[sizeof(longT) - 1] = 0;
        serial_inject(longT, sizeof(longT) - 1);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime keep7 = rtc.GetCurrentDateTime();
        tlog("  T 비숫자 연도 뒤 %u %02u:%02u · 300바이트 전문 뒤 %u %02u:%02u\n", keep6.year(), keep6.hour(),
             keep6.minute(), keep7.year(), keep7.hour(), keep7.minute());
        CHECK(keep6.year() == TRACEQ_RELEASE_YEAR && keep6.hour() == 10,
              "T 비숫자 연도는 시계를 바꾸지 않는다(2047년으로 굳던 자리)");
        CHECK(keep7.year() == TRACEQ_RELEASE_YEAR && keep7.hour() == 10,
              "T 칸 경계가 255 를 넘어도 조용히 잘못된 시계가 되지 않는다");
        // 511 에서 끊긴 전문의 꼬리가 'T…' 여도 시계를 바꾸지 않는다
        rtc_set(rel_date(10, 0, 0));
        char burst[560];
        memset(burst, 'Y', sizeof(burst));
        burst[0] = 'Q';
        const char tailCmd[] = "T2026;9;27;3;23;59;59;";       // 꼬리에 **온전한** T 명령이 들어가야 잠금이 된다
        memcpy(burst + 511, tailCmd, sizeof(tailCmd) - 1);
        serial_inject(burst, sizeof(burst));
        GUARDED(serialEvent());                          // 앞 511
        GUARDED(serialEvent());                          // 꼬리
        run_loops(1);
        const DateTime tail = rtc.GetCurrentDateTime();
        tlog("  T 꼬리 뒤 %02u:%02u (10:00 유지)\n", tail.hour(), tail.minute());
        CHECK(tail.hour() == 10, "T 꼬리는 시계를 바꾸지 않는다(값 한가운데의 'T' 로 기록 시각이 틀어지던 것)");
        as_type('D');
        touch(mgr);
        rtc_set(rel_date(10, 0, 0));
    }

    // ── BB3 P2-1: 맨 앞의 'Z' 뒤에 붙은 명령이 실행된다 ──
    //    세척관리는 연결마다 'Z'(confirm) 직후 'T…' 를 잇따라 써서 한 버퍼가 된다. 종전엔 머리가 'Z' 라 통째로 유실됐다.
    {
        power_restore();
        as_type('W');
        rtc_set(rel_date(10, 0, 0));
        char zt[56];
        snprintf(zt, sizeof(zt), "ZT%u;%u;%u;3;16;45;0;", (unsigned)TRACEQ_RELEASE_YEAR,
                 (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        logs_clear();
        serial_inject(zt, strlen(zt));
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime n = rtc.GetCurrentDateTime();
        tlog("  BB3 Z+T -> %02u:%02u 안내=%d\n", n.hour(), n.minute(), lcd_has("updated"));
        CHECK(n.hour() == 16 && n.minute() == 45 && lcd_has("updated"),
              "BB3 P2-1 'Z' 뒤에 붙은 시각 동기가 실행된다(세척관리 연결마다의 실제 전문)");
        // 서버 발급 명령도 같은 자리 — 델파이는 333ms 마다 'Z' 를 보낸다
        as_type('S');
        rtc_set(rel_date(10, 0, 0));
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        bb_washed(0x61, 61);
        card_place(&sc);
        run_loops(2);
        logs_clear();
        serial_inject("ZS555;SERZ;", 11);
        GUARDED(serialEvent());
        run_loops(2);
        Tag issued{};
        memcpy(&issued, sc.data[SECTOR0_TAG], sizeof(issued));
        tlog("  BB3 Z+S 발급 -> 번호=%u 안내=%d\n", (unsigned)issued.Number, lcd_has("new tag"));
        CHECK(issued.Number == 555 && lcd_has("new tag"),
              "BB3 P2-1 'Z' 뒤에 붙은 발급 명령도 실행된다(델파이 333ms keepalive)");
        card_remove();
        run_loops(2);
    }
    // ── BB2 P2-2 · P3-1: 시계를 뒤로 돌린 뒤의 세척 종료 ──
    {
        power_restore();
        as_type('W');
        recordOption.SetManagerDisposability(false);
        rtc_set(rel_date(14, 0, 0));
        touch(mgr);
        bb_washed(0x62, 62);
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        touch(sc);                                       // 세척 시작 14:00
        const LocalDateTime st = get_ldt(sc, SECTOR2_WASHING_START);
        CHECK(st.Time.Hour == 14, "BB2 전제: 세척 시작이 14:00 으로 기록됐다");
        rtc_set(rel_date(9, 0, 0));                      // PC·설정기·메뉴가 시계를 09:00 으로 되돌린다
        sim_advance_ms(3000);                            // 더블터치 창(2초) 밖
        logs_clear();
        touch(sc);                                       // 세척 종료
        const LocalDateTime en = get_ldt(sc, SECTOR3_WASHING_END);
        tlog("  BB2 P2-2 시작 %02u:%02u -> 종료 %02u:%02u\n", st.Time.Hour, st.Time.Minute,
             en.Time.Hour, en.Time.Minute);
        CHECK(en.Time.Hour == 14 && en.Time.Minute == 0,
              "BB2 P2-2 종료가 시작보다 앞서면 시작 시각으로 — '종료 < 시작' 기록이 안 남는다");
        // P3-1: 시작이 '미래'(1초 뒤)인 태그의 종료 터치가 시작 재실행이 되지 않는다
        bb_washed(0x63, 63);
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        rtc_set(rel_date(11, 0, 0));
        touch(sc);                                       // 시작 11:00
        rtc_set(rel_date(10, 59, 59));                   // 1초 되돌림 → 태그의 시작이 미래
        logs_clear();
        touch(sc);
        // 시작 재실행이면 **시작 기록이 10:59:59 로 덮이고 종료는 비어 있다**. 종료면 시작이 그대로다.
        const LocalDateTime st2 = get_ldt(sc, SECTOR2_WASHING_START);
        const LocalDateTime en2 = get_ldt(sc, SECTOR3_WASHING_END);
        tlog("  BB2 P3-1 미래 시작 뒤 터치: 시작 %02u:%02u:%02u 종료 %02u:%02u\n", st2.Time.Hour,
             st2.Time.Minute, st2.Time.Second, en2.Time.Hour, en2.Time.Minute);
        CHECK(st2.Time.Hour == 11 && st2.Time.Minute == 0 && en2.Time.Hour == 11,
              "BB2 P3-1 시작이 미래여도 종료 터치는 종료다(시작 기록이 덮이지 않는다)");
    }
    // ── BB2 P2-3: 시계를 뒤로 돌려도 지난 동시소독 창이 되살아나지 않는다 ──
    {
        power_restore();
        as_type('D');
        disinfectionOption.SetSimultaneousDisinfectionSlot(2);    // ★동시소독을 **켜는 것은 슬롯**(>=2)이다
        disinfectionOption.SetSimultaneousDisinfectionDelay(3);   // 3분 창
        rtc_set(rel_date(14, 0, 0));
        touch(mgr);
        bb_washed(0x64, 64);
        touch(sc);                                       // host 소독 시작 14:00 (창 14:03)
        const int cnt0 = disinfectionOption.GetCount();
        sim_advance_ms(10UL * 60 * 1000);                // 14:10 — 창 밖
        rtc_set(rel_date(9, 0, 0));                      // 되돌림 → 지난 창이 되살아난다
        bb_washed(0x65, 65);                             // 다른 번호의 스코프를 단독 소독
        logs_clear();
        touch(sc);
        const Process pb = get_process(sc);
        const int cnt1 = disinfectionOption.GetCount();
        tlog("  BB2 P2-3 되돌림 뒤 단독 소독: DC=%u 횟수 %d->%d\n", pb.DisinfectionCount, cnt0, cnt1);
        CHECK(pb.DisinfectionCount == 1 && cnt1 == cnt0 + 1,
              "BB2 P2-3 시계를 되돌려도 단독 소독은 그룹1·소독 횟수 +1(액교환 주기에서 사라지지 않는다)");
        disinfectionOption.SetSimultaneousDisinfectionDelay(0);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0);
        rtc_set(rel_date(10, 0, 0));
    }

    // ── CC1 P1-2: 소독 종료(정상 경로)도 시작보다 앞선 종료를 시작 시각으로 ──
    //    방아쇠는 사람 조작이 아니다: 시작과 종료 사이에 RTC 전지가 방전되면 시계가 2026-01-01 로 고정되고
    //    복구는 시작 경로에만 있어 종료 터치엔 안 듣는다 → 종료가 시작보다 268일 앞섰다.
    {
        power_restore();
        as_type('D');
        rtc_set(rel_date(14, 0, 0));
        touch(mgr);
        bb_washed(0x66, 66);
        touch(sc);                                       // 소독 시작 14:00
        const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
        CHECK(ds.Time.Hour == 14, "CC1 전제: 소독 시작이 14:00 으로 기록됐다");
        rtc_set(DateTime(2026, 1, 1, 0, 0, 30));         // RTC 방전 — 시계가 방전 표지로
        sim_advance_ms(3000);
        logs_clear();
        touch(sc);                                       // 소독 종료(이동 아님)
        const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
        tlog("  CC1 P1-2 방전 뒤 소독 종료 = %04u-%02u-%02u %02u:%02u\n", de.Date.Year, de.Date.Month,
             de.Date.Day, de.Time.Hour, de.Time.Minute);
        CHECK(de.Date.Year == TRACEQ_RELEASE_YEAR && de.Date.Month == TRACEQ_RELEASE_MONTH &&
              de.Time.Hour == 14,
              "CC1 P1-2 소독 종료도 시작보다 앞서지 않는다(형제 셋 모두)");
        rtc_set(rel_date(10, 0, 0));
    }

    done();
    for (;;) {}
}
