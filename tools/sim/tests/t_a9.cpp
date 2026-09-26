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

    done();
    for (;;) {}
}
