// DD3 — v2.2.24 의 "읽기 실패면 Ok! 를 안 낸다" 가 와이어에 실제로 무엇을 내보내는가(서버 S).
//  (A) 절단된 덤프의 줄 집합 · 종결자 유무 · Read Error 가 시리얼에 나가는가
//  (C) 레거시 T 시각 동기 — 세척관리·올눈이 실제로 만드는 문자열 그대로
#include "common.h"

// 출시일 기준 시각 — 절대 날짜를 쓰면 출시일이 움직일 때 뜻이 조용히 바뀐다(IsUnsynced 비교 기준이 출시일).
static inline DateTime rel_date2(uint8_t h, uint8_t m, uint8_t s) { return rel_date(h, m, s); }
static_assert(TRACEQ_RELEASE_YEAR == 2026, "덤프 블록 줄의 연도 hex(EA07) 전제 — 연도가 바뀌면 이 시험의 기대값을 고칠 것");

static SimCard sc;

// 델파이는 `MonitoringMemo.Lines[Count-2]`(= 마지막 실제 줄)만 보고 저장을 결정한다
// (MainFormSo.pas 32,551줄 · 5751줄). 그래서 "마지막 줄이 무엇인가" 가 와이어 계약이다.
static char s_last[64];
static const char *last_line()
{
    const char *p = g_serialOut;
    size_t n = strlen(p);
    while (n > 0 && (p[n - 1] == '\r' || p[n - 1] == '\n')) --n;   // 꼬리 개행 제거
    size_t b = n;
    while (b > 0 && p[b - 1] != '\n') --b;
    size_t len = n - b;
    if (len >= sizeof(s_last)) len = sizeof(s_last) - 1;
    memcpy(s_last, p + b, len);
    s_last[len] = 0;
    // '\r' 이 남아 있으면 자른다
    for (char *q = s_last; *q; ++q) if (*q == '\r') { *q = 0; break; }
    return s_last;
}

static void done_scope(SimCard &t, uint8_t uid, int no, uint8_t dc)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, dc, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date2(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, rel_date2(9, 4, 0));
    // 덤프 **마지막 두 줄**(1310·1311 = 세척 종료 담당자)을 진짜 값으로 — 안 채우면 그 줄이 원래 0 이라
    // "0 이 섞였다" 를 그 줄로 판정할 수 없다(첫 판에서 헛빨강이 났다).
    put_block(t, SECTOR3_WASHING_START_MANAGER_KEY, "WSKEY", 5);
    put_block(t, SECTOR3_WASHING_START_MANAGER_NAME, "WSNAME", 6);
    put_block(t, SECTOR4_WASHING_END_MANAGER_KEY, "WEKEY", 5);
    put_block(t, SECTOR4_WASHING_END_MANAGER_NAME, "WENAME", 6);
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date2(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 2, rel_date2(9, 23, 0));
    if (dc == 2)
    {
        // 2차(도착) 소독기 = 3번. PC 는 이 두 블록만 값으로 쓴다(0x1F1C · 0x2320).
        set_record(t, SECTOR7_DISINFECTION_START, 3, rel_date2(9, 30, 0));
        set_record(t, SECTOR8_DISINFECTION_END, 3, rel_date2(9, 48, 0));
    }
}

int main()
{
    rtc_set(rel_date2(10, 0, 0));
    boot('S');
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);

    // ── A1 온전한 덤프(DC=2) — 줄 집합의 기준선 ──
    {
        done_scope(sc, 0x41, 41, 2);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        const bool ok = serial_has("Ok!");
        const bool s7 = serial_has("1F1C0300EA07");
        const bool s8 = serial_has("23200300EA07");
        tlog("  A1 DC=2 온전: Ok!=%d 1F1C(2차시작)=%d 2320(2차종료)=%d 마지막줄=[%s]\n",
             ok, s7, s8, last_line());
        CHECK(ok && s7 && s8, "A1 DC=2 온전한 덤프에는 2차 소독 블록 둘과 Ok! 가 함께 나간다");
        CHECK(strcmp(last_line(), "Ok!") == 0,
              "A1 온전한 덤프의 **마지막 줄**이 Ok! 다 — 델파이 Lines[Count-2] 계약(뒤에 한 줄만 더 붙어도 저장 안 됨)");
    }
    // ── A2 온전한 덤프(DC=1) — 2차 블록이 **아예 없다** ──
    {
        sim_advance_ms(60UL * 1000);
        done_scope(sc, 0x42, 42, 1);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        const bool ok = serial_has("Ok!");
        const bool any7 = serial_has("1F1C");
        const bool any8 = serial_has("2320");
        tlog("  A2 DC=1 온전: Ok!=%d 1F1C 줄=%d 2320 줄=%d\n", ok, any7, any8);
        CHECK(ok, "A2 DC=1 온전한 덤프에는 Ok! 가 나간다");
        CHECK(!any7 && !any8, "A2 DC=1 덤프는 2차 소독 블록 줄을 **한 줄도** 내지 않는다(줄 집합이 DC=2 와 다르다)");
    }
    // ── A3 접촉 중 이탈(DC=2) — 절단점을 훑어 "2차 블록은 진짜 값 + Ok! 없음" 자리를 센다 ──
    {
        static const int16_t cuts[] = {24, 30, 38, 45, 52, 58};
        // 읽기 실패가 실제로 난 절단점(= 덤프에 0 이 섞인 자리)만 센다. 덤프를 다 낸 뒤의 이탈은
        // 읽기 실패가 아니라 커밋 실패라 종전과 같은 ③(재접촉) 자리다.
        uint8_t cutDump = 0, cutDumpWithOk = 0, realNoOk = 0, readErrOnWire = 0;
        for (uint8_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i)
        {
            sim_advance_ms(60UL * 1000);
            done_scope(sc, (uint8_t)(0x50 + i), 50 + i, 2);
            sc.removeAfterOps = cuts[i];
            logs_clear();
            serial_inject("Z", 1);
            touch(sc);
            const bool ok = serial_has("Ok!");
            const bool s7 = serial_has("1F1C0300EA07");
            const bool s8 = serial_has("23200300EA07");
            // 0 으로 나간 블록이 있다 = 덤프가 절단됐다(마지막 줄 1311 이 0 이면 확실)
            const bool zeroLine = serial_has("131100000000000000000000000000000000;");
            const Process p = get_process(sc);
            if (zeroLine) { ++cutDump; if (ok) ++cutDumpWithOk; if (s7 && s8 && !ok) ++realNoOk; }
            if (serial_has("Read Error")) ++readErrOnWire;
            tlog("  A3 n=%d 0블록=%d Ok!=%d 1F1C진짜=%d 2320진짜=%d 태그보존(WS=%u DC=%u) LCD오류=%d 마지막줄=[%s]\n",
                 (int)cuts[i], zeroLine, ok, s7, s8, p.WashingStatus, p.DisinfectionCount,
                 lcd_has("Read Error"), last_line());
        }
        tlog("  A3 합계: 0섞인 덤프 %u · 그중 Ok! %u · '2차 진짜 + Ok!없음' %u\n",
             cutDump, cutDumpWithOk, realNoOk);
        CHECK(cutDump > 0 && cutDumpWithOk == 0, "A3 0 이 섞인 덤프에는 Ok! 가 한 자리도 나가지 않는다(v2.2.24)");
        CHECK(realNoOk > 0, "A3 그 덤프에 2차 소독 블록(0x1F1C·0x2320)은 **진짜 값으로** 이미 나가 있다");
        CHECK(readErrOnWire == 0, "A3 'Read Error' 는 시리얼로 나가지 않는다 — PC 는 종결자도 사유도 못 받는다");
    }
    // ── A4 절단 뒤 다음 스코프가 DC=1 이면, 앞 덤프의 2차 블록을 덮는 줄이 **하나도 없다** ──
    {
        sim_advance_ms(60UL * 1000);
        done_scope(sc, 0x60, 60, 2);
        sc.removeAfterOps = 52;
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        const bool cutHas7 = serial_has("1F1C0300EA07");
        const bool cutOk = serial_has("Ok!");

        sim_advance_ms(60UL * 1000);
        done_scope(sc, 0x61, 61, 1);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        const bool nextOk = serial_has("Ok!");
        const bool nextAny7 = serial_has("1F1C");
        const bool nextAny8 = serial_has("2320");
        tlog("  A4 절단덤프 1F1C진짜=%d Ok!=%d → 다음(DC=1) Ok!=%d 1F1C=%d 2320=%d 마지막줄=[%s]\n",
             cutHas7, cutOk, nextOk, nextAny7, nextAny8, last_line());
        // ★이것이 세척관리가 덤프마다 누적 블록을 비워야 하는 이유다 — 리더가 섹터 7·8 을 항상 내보내는 쪽으로
        //  고치면 안 된다(새 소독 시작이 그 블록을 안 지우므로 옛 이동 기록이 같은 유령 행을 만든다).
        CHECK(cutHas7 && !cutOk && nextOk && !nextAny7 && !nextAny8,
              "A4 앞 덤프의 2차 소독 블록은 다음(DC=1) 덤프의 어느 줄로도 덮이지 않는다(PC 가 비워야 한다)");
    }
    // ── C 레거시 T — 세척관리·올눈이 실제로 만드는 문자열 그대로 ──
    {
        sim_advance_ms(60UL * 1000);
        // 세척관리: rfid_serial.py:1499-1503 — f'T{y};{m};{d};{dow};{H};{M};{S};' (자리수 채움 없음 · dow 0=일)
        //  arduino_manager.py:157-180 이 연결마다 confirm()('Z') 직후에 보내므로 한 버퍼로 붙는다.
        logs_clear();
        serial_inject("ZT2026;9;27;0;16;30;0;", 22);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime a = rtc.GetCurrentDateTime();
        tlog("  C1 세척관리 'Z'+T → %04u-%02u-%02u %02u:%02u:%02u  updated(LCD)=%d 시리얼=%d\n",
             a.year(), a.month(), a.day(), a.hour(), a.minute(), a.second(),
             lcd_has("updated"), serial_has("updated"));
        CHECK(a.year() == 2026 && a.month() == 9 && a.day() == 27 && a.hour() == 16 && a.minute() == 30,
              "C1 세척관리 실제 형식(무패딩·4자리연도·dow 0=일)이 그대로 반영된다");
        CHECK(!serial_has("updated"), "C1 'updated' 는 시리얼로 안 나간다(PC 는 성공 여부를 모른다)");

        // 올눈: MainFormSo.pas:15892-15895 — 'T'+IntToStr(Year)+';'…+IntToStr(Sec)+';' (Week=DayOfWeek 1=일)
        logs_clear();
        serial_inject("T2026;9;27;1;16;45;5;", 21);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime b = rtc.GetCurrentDateTime();
        tlog("  C2 올눈 T(요일 1=일) → %02u:%02u:%02u\n", b.hour(), b.minute(), b.second());
        CHECK(b.hour() == 16 && b.minute() == 45, "C2 올눈 실제 형식(요일 1~7)도 그대로 반영된다 — 요일 칸은 버린다");

        // 연도 2자리를 보내면(옛 C 펌웨어 형식) 거부된다 — 두 PC 모두 4자리라 도달하지 않는 갈래
        logs_clear();
        serial_inject("T26;9;27;0;11;11;11;", 20);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime c = rtc.GetCurrentDateTime();
        tlog("  C3 2자리 연도 → %02u:%02u (16:45 유지) Invalid=%d\n", c.hour(), c.minute(), lcd_has("Invalid"));
        CHECK(c.hour() == 16 && c.minute() == 45 && lcd_has("Invalid DateTime"),
              "C3 연도 2자리는 거부(시계 유지) — 4자리 계약");

        // 칸이 6개(꼬리 ';' 없음)면 거부 — 두 PC 는 7칸 + 꼬리 ';' 를 보낸다
        logs_clear();
        serial_inject("T2026;9;27;0;12;12", 18);
        GUARDED(serialEvent());
        run_loops(1);
        const DateTime d = rtc.GetCurrentDateTime();
        tlog("  C4 칸 부족 → %02u:%02u 유지 Invalid=%d\n", d.hour(), d.minute(), lcd_has("Invalid"));
        CHECK(d.hour() == 16 && d.minute() == 45, "C4 칸이 모자라면 시계를 건드리지 않는다");
    }
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
