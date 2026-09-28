// 14회차 II-A(main.cpp) 잠금.
//  ① 177번지(액교환일 미룸) 손상값 정리는 **도장 블록보다 먼저** — 뒤에 두면(v2.2.15 에 내가 옮겼다) 판 바꿈 유지
//     갈래가 손상값을 '미룸 있음' 으로 읽어 교환일이 빈 채 굳고 스스로 낫지 않는다
//  ② 이어 붙이기의 새 머리 판정은 조각 앞의 keepalive 'Z' 를 건너뛴다(형제와 같은 규칙)
//  ③ [14차 판정] 서버는 'Z' 가 앞에 붙은 JSON 을 버린다 — 현장 발신자 없음 · 그 항을 지우면 'Z' 인증이 빠진다
#include "common.h"

static SimCard b, m;

static void pump(uint32_t ms)
{
    const uint32_t end = g_ms + ms;
    while (g_ms < end)
    {
        GUARDED(loop());
        if (Serial.available() > 0) GUARDED(serialEvent());
        sim_advance_ms(20);
    }
}
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
// 유지 업그레이드(도장 불일치 · 쓰던 D 기기 · 무응답 10초 = 유지) — 교환일 비움 + 177번지 값 v
static void keep_upgrade_with_pending(uint8_t v, LocalDateTime &cdOut, uint8_t &pendOut)
{
    deviceOption.SetType('D');
    for (int i = 169; i <= 176; ++i) EEPROM.update(i, 0);   // 교환일 비움(클리어 태그를 안 쓰던 기기)
    EEPROM.update(177, v);
    EEPROM.put((int)4088, (uint32_t)0);                      // 새 펌웨어 업로드 흉내(도장 불일치)
    rtc_set(rel_date(10, 0, 0));
    hard_reset(false, 0);                                    // 버튼 무응답 → 유지
    pendOut = disinfectionOption.GetClearPending();
    run_loops(2);
    cdOut = disinfectionOption.GetClearDateTime();
}
static bool same_day(const LocalDateTime &x, const LocalDateTime &y)
{
    return x.Date.Year == y.Date.Year && x.Date.Month == y.Date.Month && x.Date.Day == y.Date.Day;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');

    // ── ① 177 손상값 × 판 바꿈 유지 ──
    {
        const LocalDateTime exp = DisinfectionOption::OneMonthBefore(DefaultRtc::ToLocalDateTime(rel_date(10, 0, 0)));
        LocalDateTime cd{}; uint8_t pend = 0;
        keep_upgrade_with_pending(0x00, cd, pend);
        tlog("  1 대조(177=0): 교환일 %u-%u-%u pending=%u\n", cd.Date.Year, cd.Date.Month, cd.Date.Day, pend);
        CHECK(same_day(cd, exp), "1 대조: 177=0 이면 유지 업그레이드가 교환일 1개월 전을 넣는다");
        keep_upgrade_with_pending(0x05, cd, pend);
        tlog("  1 (177=5): 교환일 %u-%u-%u pending=%u\n", cd.Date.Year, cd.Date.Month, cd.Date.Day, pend);
        CHECK(same_day(cd, exp), "1a 177=5(손상)여도 유지 업그레이드가 교환일 1개월 전을 넣는다(정리가 도장 블록보다 먼저)");
        keep_upgrade_with_pending(0xFF, cd, pend);
        tlog("  1 (177=FF): 교환일 %u-%u-%u pending=%u\n", cd.Date.Year, cd.Date.Month, cd.Date.Day, pend);
        CHECK(same_day(cd, exp), "1b 177=FF 여도 같다");
        hard_reset(false, 2);
        CHECK(!disinfectionOption.IsClearDateTimeEmpty(), "1c 같은 판 재부팅 뒤에도 교환일이 채워져 있다");
    }

    // ── ② 이어 붙이기 — 새 레코드 조각이 'Z' 로 시작해도 새 머리로 본다 ──
    {
        deviceOption.SetType('G'); hard_reset(false);
        rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
        const uint32_t t1 = g_ms + 50;
        static const char b1[] = "G10000;G22026;9;23;4;10;05;0;G3CCC";
        static const char b2[] = "ZG10000;G22026;9;23;4;13;45;0;G3DDDDD;NAMED;;G4SUBJD;;;G5;";
        serial_queue(b1, sizeof(b1) - 1, t1);
        serial_queue(b2, sizeof(b2) - 1, t1 + 1800);
        pump(8000);
        fresh_scope(b, 0x61, 61);
        logs_clear(); buzz_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  2 키=[%.12s] 이름=[%.10s] 항목=[%.8s] %02u:%02u Status=%u\n",
             (const char *)b.data[SECTOR2_PATIENT_KEY], (const char *)b.data[SECTOR2_PATIENT_NAME],
             (const char *)b.data[SECTOR15_EXAMINATION_SUBJECT], d.Time.Hour, d.Time.Minute, get_process(b).Status);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "DDDDD", 5) == 0 &&
              memcmp(b.data[SECTOR2_PATIENT_NAME], "NAMED", 5) == 0 && d.Time.Hour == 13 && d.Time.Minute == 45,
              "2 'Z' 가 앞에 붙은 새 레코드 조각도 앞 미완 레코드와 섞이지 않는다(새 머리 판정이 'Z' 를 건너뛴다)");
    }

    // ── ③ [14차 판정 · 재론 금지] 서버는 Z+JSON 을 버린다(W 는 받는다) — 바꾸면 이 CHECK 를 판정과 함께 뒤집는다 ──
    {
        static const char js[] = "Z\x02{\"cmd\":\"cfg_get_config\"}\x03";
        deviceOption.SetType('W'); hard_reset(false);
        logs_clear();
        serial_inject(js, sizeof(js) - 1); GUARDED(serialEvent());
        const bool wGot = serial_has("device_type");
        deviceOption.SetType('S'); hard_reset(false);
        logs_clear();
        serial_inject(js, sizeof(js) - 1); GUARDED(serialEvent());
        const bool sGot = serial_has("device_type");
        tlog("  3 Z+JSON 응답: W=%u S=%u\n", (unsigned)wGot, (unsigned)sGot);
        CHECK(wGot, "3 대조: 세척기는 Z+JSON 에 응답한다");
        CHECK(!sGot, "3a (판정) 서버는 Z+JSON 을 버린다 — 현장 발신자 없음 · 'Z' 인증 항과 한 몸(main.cpp rawHead)");
    }

    // ── ④ 레거시 발급 `M` 의 16바이트 이름 — 마지막 바이트가 태그에 남는다(II-E: `char[16]` 은 15바이트만 · 게이트웨이 형제는 [17]) ──
    {
        deviceOption.SetType('S'); hard_reset(false, 2);
        serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
        make_tag(m, 0x72, MANAGER_TYPE_TAG, 0, "OLD", "OLDNAME");
        static const char cmd[] = "MND0001;ABCDEFGHIJKLMNOP;";   // 이름 16바이트(cp949 한글 8자와 같은 길이)
        logs_clear();
        serial_inject(cmd, sizeof(cmd) - 1);
        card_place(&m);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  4 담당자 이름 16B 발급: 태그 이름=[%.16s] [15]=%02X new=%u\n",
             (const char *)m.data[SECTOR1_TAG_SERIAL], m.data[SECTOR1_TAG_SERIAL][15], (unsigned)lcd_has("new tag"));
        CHECK(lcd_has("new tag"), "4 전제: 발급 자체는 된다");
        CHECK(m.data[SECTOR1_TAG_SERIAL][15] == 'P', "4a 16바이트 이름의 마지막 바이트가 태그에 남는다(발급 버퍼 [17])");
    }

    // ── ⑤ JSON device_type 소문자 — 서버가 세척기로 바뀌지 않는다 · 대문자로 정규화 · W/D/S/G 밖은 무시 (II-D P3-10) ──
    {
        deviceOption.SetType('S'); hard_reset(false, 2);
        const uint16_t rc0 = g_resetCount;
        static const char js1[] = "{\"cmd\":\"cfg_set_config\",\"device_type\":\"s\"}";
        serial_inject(js1, sizeof(js1) - 1); GUARDED(serialEvent()); run_loops(1);
        const char t1 = deviceOption.GetType(); const uint16_t rc1 = g_resetCount;
        static const char js2[] = "{\"cmd\":\"cfg_set_config\",\"device_type\":\"x\"}";
        serial_inject(js2, sizeof(js2) - 1); GUARDED(serialEvent()); run_loops(1);
        const char t2 = deviceOption.GetType(); const uint16_t rc2 = g_resetCount;
        static const char js3[] = "{\"cmd\":\"cfg_set_config\",\"device_type\":\"g\"}";
        serial_inject(js3, sizeof(js3) - 1); GUARDED(serialEvent()); run_loops(1);
        const char t3 = deviceOption.GetType();
        tlog("  5 device_type: \"s\"→%c(reset+%u) · \"x\"→%c(reset+%u) · \"g\"→%c(reset+%u)\n",
             t1, (unsigned)(rc1 - rc0), t2, (unsigned)(rc2 - rc1), t3, (unsigned)(g_resetCount - rc2));
        CHECK(t1 == 'S' && rc1 == rc0, "5a \"s\"(소문자 · 같은 타입)는 서버를 그대로 둔다 — 종전엔 세척기로 바뀌며 재시작");
        CHECK(t2 == 'S' && rc2 == rc1, "5b W/D/S/G 밖(\"x\")은 무시한다");
        CHECK(t3 == 'G', "5c 양성대조: \"g\" 는 대문자로 정규화되어 G 로 바뀐다");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
