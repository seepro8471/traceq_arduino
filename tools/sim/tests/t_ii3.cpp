// 14회차 II-A(main.cpp) 잠금.
//  ① 177번지(액교환일 미룸) 손상값 정리는 **도장 블록보다 먼저** — 뒤에 두면(v2.2.15 에 내가 옮겼다) 판 바꿈 유지
//     갈래가 손상값을 '미룸 있음' 으로 읽어 교환일이 빈 채 굳고 스스로 낫지 않는다
//  ② 이어 붙이기의 새 머리 판정은 조각 앞의 keepalive 'Z' 를 건너뛴다(형제와 같은 규칙)
//  ③ [14차 판정] 서버는 'Z' 가 앞에 붙은 JSON 을 버린다 — 설정기는 PSOk 응답에 'Z' 를 보내지만 JSON 과 한 버퍼에 붙는 것은 150ms 안 겹침뿐 · 그 항을 지우면 'Z' 인증이 빠진다
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

    // ── ②b 'ZZ'(keepalive 둘)가 붙은 조각 · ②c 미완 조각 둘 뒤 완전한 조각 — 건너뜀이 while 이고 버리는 길이(drop)가 맞아야 한다(15차 III-H z2·z3) ──
    {
        deviceOption.SetType('G'); hard_reset(false);
        rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
        const uint32_t t1 = g_ms + 50;
        static const char b1[] = "G10000;G22026;9;23;4;10;05;0;G3CCC";
        static const char b2[] = "ZZG10000;G22026;9;23;4;14;15;0;G3EEEEE;NAMEE;;G4SUBJE;;;G5;";
        serial_queue(b1, sizeof(b1) - 1, t1);
        serial_queue(b2, sizeof(b2) - 1, t1 + 1800);
        pump(8000);
        fresh_scope(b, 0x62, 62);
        logs_clear(); buzz_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  2b 'ZZ': 키=[%.12s] %02u:%02u Status=%u\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             d.Time.Hour, d.Time.Minute, get_process(b).Status);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "EEEEE", 5) == 0 && d.Time.Hour == 14 && d.Time.Minute == 15,
              "2b 'ZZ' 가 붙은 새 조각도 앞 미완 레코드와 섞이지 않는다(건너뜀은 while)");

        hard_reset(false);
        rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
        const uint32_t t2 = g_ms + 50;
        // 앞 미완 → 'Z'+새 머리(미완 · 여기서 앞 것을 버리며 'Z' 만큼 더 버려야 한다) → 그 **이어지는 조각**(머리 아님)
        //  buffer 를 옮길 때 버리는 길이가 'Z' 를 뺀 옛 길이면 NUL 뒤에 셋째 조각이 붙어 환자 전체가 사라지고 폴백이 됐다.
        static const char c1[] = "G10000;G22026;9;23;4;10;05;0;G3CCC";
        static const char c2[] = "ZG10000;G22026;9;23;4;15;25;0;G3GGGGG;";
        static const char c3[] = "NAMEG;;G4SUBJG;;;G5;";
        serial_queue(c1, sizeof(c1) - 1, t2);
        serial_queue(c2, sizeof(c2) - 1, t2 + 1800);
        serial_queue(c3, sizeof(c3) - 1, t2 + 3600);
        pump(10000);
        fresh_scope(b, 0x63, 63);
        logs_clear(); buzz_clear();
        touch(b);
        const LocalDateTime d3 = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  2c 미완 둘 뒤 완전: 키=[%.12s] %02u:%02u Status=%u NoPatient=%u\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             d3.Time.Hour, d3.Time.Minute, get_process(b).Status, (unsigned)lcd_has("No Patient Info"));
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "GGGGG", 5) == 0 && d3.Time.Hour == 15 && d3.Time.Minute == 25 &&
              get_process(b).Status == 1,
              "2c 미완 조각이 둘 이어 와도 셋째 완전한 레코드의 환자가 기록된다(폴백으로 빠지지 않는다 · drop 길이)");

        // ②d 'Z' 가 붙은 **G1 조각**만 먼저 오고 나머지가 1초 넘게 늦게 와도 본체번호(G1)를 잃지 않는다 — 전문 판정·기다림도 'Z' 를
        //  건너뛴 머리로(16차 IV-A: `IsGatewayFrame(buffer)`·`NeedsMoreBytes(buffer)` 가 날것이라 그 조각을 버리고 설정값으로 폴백했다)
        hard_reset(false);
        deviceOption.SetNumber(7);
        rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
        const uint32_t t3 = g_ms + 50;
        static const char e1[] = "ZG10003;";
        static const char e2[] = "G22026;9;23;4;16;05;0;G3HHHHH;NAMEH;;G4SUBJH;;;G5;";
        serial_queue(e1, sizeof(e1) - 1, t3);
        serial_queue(e2, sizeof(e2) - 1, t3 + 1800);
        pump(8000);
        fresh_scope(b, 0x64, 64);
        logs_clear(); buzz_clear();
        touch(b);
        const int gwNo = (int)(b.data[SECTOR1_GATEWAY][0] | (b.data[SECTOR1_GATEWAY][1] << 8));
        tlog("  2d 'Z'+G1 조각: 본체번호=%d 키=[%.5s] Status=%u\n", gwNo, (const char *)b.data[SECTOR2_PATIENT_KEY], get_process(b).Status);
        CHECK(gwNo == 3 && memcmp(b.data[SECTOR2_PATIENT_KEY], "HHHHH", 5) == 0 && get_process(b).Status == 1,
              "2d 'Z' 뒤 G1 조각이 먼저 와도 본체번호 3 과 환자가 기록된다(전문 판정이 'Z' 를 건너뛴다)");
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

    // ── ④b 형제: 레거시 스코프 발급 `S` 의 16바이트 시리얼 — 14차 [17] 봉합이 이 자리만 리터럴 16 으로 남겨 [15] 가 00 이었다(15차) ──
    {
        deviceOption.SetType('S'); hard_reset(false, 2);
        serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
        make_tag(m, 0x73, SCOPE_TYPE_TAG, 0, "OLD", "OLDSER");
        static const char cmd[] = "S12;ABCDEFGHIJKLMNOP;";   // 시리얼 16바이트
        logs_clear();
        serial_inject(cmd, sizeof(cmd) - 1);
        card_place(&m);
        GUARDED(serialEvent());
        card_remove(); run_loops(4);
        tlog("  4b 스코프 시리얼 16B 발급: 태그 시리얼=[%.16s] [15]=%02X 번호=%d new=%u\n",
             (const char *)m.data[SECTOR1_TAG_SERIAL], m.data[SECTOR1_TAG_SERIAL][15],
             (int)(m.data[SECTOR0_TAG][0] | (m.data[SECTOR0_TAG][1] << 8)), (unsigned)lcd_has("new tag"));
        CHECK(lcd_has("new tag"), "4b 전제: 스코프 발급 자체는 된다");
        CHECK(m.data[SECTOR1_TAG_SERIAL][15] == 'P', "4b 16바이트 시리얼의 마지막 바이트가 태그에 남는다(S 발급도 sizeof — 14차 형제 누락)");
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
