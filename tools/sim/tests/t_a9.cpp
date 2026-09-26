// Y2 잠금 — 연속된 EEPROM 쓰기 사이에 전원이 나가도 남는 상태가 안전한가(찢긴 값이 기록에 실리지 않는가).
// 장치: fake_hw 의 전원 차단 주입(g_eepromCutAfter = n → n번째 바이트 쓰기까지만 남고 그 뒤는 사라짐) → hard_reset.
// 절단점 n 을 0..K 전부 훑는다(K = 차단 없는 같은 조작의 총 쓰기 수). 되돌리면 여기서 빨강이 나야 한다.
#include "common.h"

static SimCard mgrA, mgrB, clr, sc;

static LocalDateTime ldt(uint16_t y, uint8_t mo, uint8_t d, uint8_t h, uint8_t mi, uint8_t s)
{
    return LocalDateTime{LocalDate{y, mo, d}, LocalTime{h, mi, s}};
}
static bool ldt_same(const LocalDateTime &a, const LocalDateTime &b)
{
    return ldt_eq(a, b.Date.Year, b.Date.Month, b.Date.Day, b.Time.Hour, b.Time.Minute, b.Time.Second);
}
static void reboot() { power_restore(); hard_reset(false, 2); }
static void fresh_scope()
{
    make_tag(sc, 0x11, SCOPE_TYPE_TAG, 11, "SC0011", "S0011");
    set_process(sc, Process{0, 0, 1, 0, 1, false, 0, 1});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');
    make_tag(mgrA, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    make_tag(mgrB, 0x02, MANAGER_TYPE_TAG, 8, "ND09999", "PARKSS");
    make_tag(clr, 0x03, CLEAR_TYPE_TAG, 0, "", "");
    touch(mgrA);

    // ── Y2 P3-1 클리어 태그: 교환일 8바이트가 찢겨 '옛 일 + 새 월' 이 남으면 그 뒤 소독 기록에 실린다 ──
    //    미룸을 먼저 세우므로, 절단 뒤 교환일은 옛 값 · 새 값 · (미룸 복구로) 재개 시각 중 하나여야 한다.
    {
        const LocalDateTime oldClear = ldt(2026, 8, 15, 9, 0, 0);
        auto pre = [&]() {
            power_restore();
            rtc_set(rel_date(10, 0, 0));
            disinfectionOption.SetCount(5);
            disinfectionOption.SetClearDateTime(oldClear);
            disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
            disinfectionOption.SetClearCount(3);
        };
        pre();
        g_eepromWrites = 0;
        touch(clr);
        const uint32_t K = g_eepromWrites;
        const LocalDateTime newClear = disinfectionOption.GetClearDateTime();
        tlog("  P3-1 클리어 터치 쓰기 수 K=%lu\n", (unsigned long)K);
        CHECK(disinfectionOption.GetCount() == 0 && disinfectionOption.GetClearCount() == 4 && K >= 4 &&
              ldt_same(newClear, ldt(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY, 10, 0, 0)),
              "Y2 대조: 차단 없는 클리어 = 횟수 0·클리어횟수 4·교환일 10:00");

        uint8_t torn = 0;
        int firstTorn = -1;
        for (uint32_t n = 0; n <= K; ++n)
        {
            pre();
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            touch(clr);
            power_restore();
            sim_advance_ms(3UL * 3600 * 1000);          // 3시간 꺼져 있다가 다시 켠다
            const LocalDateTime resume = rtc.GetCurrentLocalDateTime();
            hard_reset(false, 2);
            const LocalDateTime d = disinfectionOption.GetClearDateTime();
            const bool known = ldt_same(d, oldClear) || ldt_same(d, newClear) || ldt_same(d, resume);
            if (!known) { ++torn; if (firstTorn < 0) firstTorn = (int)n; }
            tlog("  P3-1 n=%lu: %04u-%02u-%02u %02u:%02u:%02u pending=%u %s\n", (unsigned long)n, d.Date.Year,
                 d.Date.Month, d.Date.Day, d.Time.Hour, d.Time.Minute, d.Time.Second,
                 disinfectionOption.GetClearPending(), known ? "" : "★섞인 날짜");
        }
        tlog("  P3-1 섞인 날짜 절단점 %u / %lu\n", torn, (unsigned long)(K + 1));
        CHECK(torn == 0, "Y2 P3-1 클리어 태그: 어느 절단점에서 끊겨도 교환일은 옛 값·새 값·재개 시각 중 하나(섞인 날짜 없음)");

        // [5차 판정 · 재론 금지 = Y2 P3-3] 미룸이 남은 채 끊기면 교환일은 '재개 시각' 이 된다 — 날짜가 안 남는 쪽보다 낫다.
        pre();
        g_eepromWrites = 0; g_eepromCutAfter = 2;       // 날짜 도중
        touch(clr);
        power_restore();
        sim_advance_ms(3UL * 3600 * 1000);
        hard_reset(false, 2);
        tlog("  P3-3 미룸 복구 뒤 pending=%u 시각=%02u:%02u\n", disinfectionOption.GetClearPending(),
             disinfectionOption.GetClearDateTime().Time.Hour, disinfectionOption.GetClearDateTime().Time.Minute);
        CHECK(!disinfectionOption.HasPendingClear(), "Y2 P3-3 절단 뒤 다음 부팅이 미룸을 해소한다(미룸이 남아 떠돌지 않는다)");

        // 섞인 날짜가 소독 기록(태그 상세 56)에 실리지 않는다
        pre();
        g_eepromWrites = 0; g_eepromCutAfter = (firstTorn >= 0 ? (int32_t)firstTorn : 2);
        touch(clr);
        reboot();
        const LocalDateTime after = disinfectionOption.GetClearDateTime();
        fresh_scope();
        touch(sc);
        LocalDateTime det{};
        memcpy(&det, sc.data[SECTOR14_DISINFECTION_DETAIL], sizeof(det));
        tlog_ldt("P3-1 절단 뒤 첫 소독 태그 상세 교환일", det);
        CHECK(ldt_same(det, after), "Y2 P3-1 태그 상세(56)의 액교환일은 저장된 교환일 그대로");
    }

    // ── Y2 P3-2 담당자 등록(A→B): 표지를 먼저 내리고 키·이름을 다 넣은 뒤 올린다 ──
    //    절단 뒤 '담당자 있음' 이면 그 쌍은 정확히 A 이거나 B 여야 한다(반쪽 담당자가 기록에 실리지 않는다).
    {
        unsigned char kA[16]{}, kB[16]{}, nA[16]{}, nB[16]{};
        memcpy(kA, mgrA.data[SECTOR0_TAG] + 2, 14);
        memcpy(kB, mgrB.data[SECTOR0_TAG] + 2, 14);
        memcpy(nA, mgrA.data[SECTOR1_TAG_SERIAL], 16);
        memcpy(nB, mgrB.data[SECTOR1_TAG_SERIAL], 16);

        power_restore();
        touch(mgrA);
        g_eepromWrites = 0;
        touch(mgrB);
        const uint32_t K = g_eepromWrites;
        tlog("  P3-2 담당자 A→B 쓰기 수 K=%lu\n", (unsigned long)K);
        CHECK(K >= 4 && managerOption.HasData(), "Y2 대조: A→B 등록은 키·이름 여러 바이트를 쓴다");

        uint8_t mixed = 0;
        int firstMixed = -1;
        for (uint32_t n = 0; n <= K; ++n)
        {
            power_restore();
            touch(mgrA);
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            touch(mgrB);
            reboot();
            unsigned char k[16]{}, nm[16]{};
            managerOption.GetKey(k, 14);
            managerOption.GetName(nm, 16);
            const bool isA = memcmp(k, kA, 14) == 0 && memcmp(nm, nA, 16) == 0;
            const bool isB = memcmp(k, kB, 14) == 0 && memcmp(nm, nB, 16) == 0;
            const bool has = managerOption.HasData();
            if (has && !isA && !isB) { ++mixed; if (firstMixed < 0) firstMixed = (int)n; }
            tlog("  P3-2 n=%lu: has=%d key=%.14s name=%.16s %s\n", (unsigned long)n, has, (const char *)k,
                 (const char *)nm, (has && !isA && !isB) ? "★반쪽 담당자" : "");
        }
        tlog("  P3-2 반쪽 담당자 절단점 %u / %lu\n", mixed, (unsigned long)(K + 1));
        CHECK(mixed == 0, "Y2 P3-2 담당자 등록: '담당자 있음' 으로 남은 절단점의 쌍은 정확히 A 이거나 B");

        // 반쪽이 남은 절단점에서는 소독 시작이 거부되고(담당자 없음), 다시 대면 복구된다
        power_restore();
        recordOption.SetManagerDisposability(false);
        touch(mgrA);
        g_eepromWrites = 0; g_eepromCutAfter = (int32_t)(K / 2);
        touch(mgrB);
        reboot();
        fresh_scope();
        logs_clear();
        touch(sc);
        const bool rejected = lcd_has("No Manager Info");
        tlog("  P3-2 절단 뒤 소독 시작: 거부=%d has=%d\n", rejected, managerOption.HasData());
        CHECK(rejected == !managerOption.HasData(), "Y2 P3-2 반쪽으로 남으면 '담당자 없음' 으로 거부(반쪽으로 기록하지 않는다)");
        touch(mgrB);
        logs_clear();
        touch(sc);
        CHECK(managerOption.HasData() && memcmp(sc.data[SECTOR5_DISINFECTION_START_MANAGER_NAME], nB, 16) == 0,
              "Y2 P3-2 다시 대면 온전한 담당자로 복구된다");
    }

    // ── Y2 P3-4 같은 판 + RIGHT 소거 중 전원 차단 → 다음 부팅이 다시 묻거나 재소거(반쯤 지워진 설정으로 조용히 기동 금지) ──
    {
        auto pre = [&]() {
            power_restore();
            buttons_script("");
            for (int i = 0; i < EEPROM.length(); ++i) EEPROM.update(i, 0);
            hard_reset(false, 0);
            deviceOption.SetType('D');
            deviceOption.SetNumber(5);
            alarmOption.SetTimeSlot2(18);
            disinfectionOption.SetMaximumCount(30);
            EEPROM.put((int)4088, fw_stamp());
            touch(mgrA);
        };
        pre();
        buttons_script("RRRRrRS");
        g_eepromWrites = 0;
        hard_reset(false, 0);
        const uint32_t K = g_eepromWrites;
        tlog("  P3-4 RIGHT 소거 쓰기 수 K=%lu\n", (unsigned long)K);
        CHECK(K >= 20 && deviceType == 'W' && deviceOption.GetNumber() == 1, "Y2 대조: 차단 없는 RIGHT 소거 = 기본값(W·1번)");

        uint8_t silent = 0;
        for (uint32_t n = 0; n <= K; ++n)
        {
            pre();
            buttons_script("RRRRrRS");
            g_eepromWrites = 0; g_eepromCutAfter = (int32_t)n;
            hard_reset(false, 0);
            power_restore();
            buttons_script("");
            logs_clear();
            hard_reset(false, 2);
            const bool asked = lcd_has("Keep settings") || lcd_has("Initializing");
            const bool oldKept = deviceType == 'D' && deviceOption.GetNumber() == 5 &&
                                 alarmOption.GetTimeSlot2() == 18 && managerOption.HasData();
            if (!asked && !oldKept) ++silent;
            tlog("  P3-4 n=%lu %c type=%c no=%d\n", (unsigned long)n, asked ? 'H' : oldKept ? 'K' : 'S', deviceType,
                 deviceOption.GetNumber());
        }
        tlog("  P3-4 조용히 다른 설정으로 기동한 절단점 %u / %lu\n", silent, (unsigned long)(K + 1));
        CHECK(silent == 0, "Y2 P3-4 소거 중 끊겨도 다음 부팅이 다시 묻거나 재소거한다(반쯤 지워진 설정으로 기동 금지)");
    }

    done();
    for (;;) {}
}
