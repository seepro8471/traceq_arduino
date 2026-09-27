// DD1 — 8회차 봉합(46066ba^..beb8835) 재추적. 사본에서만 돈다.
//  §1 GatewaySerialEvent 인자가 cmd -> buffer 로 바뀌며 맨 앞 'Z' 뒤의 G1 본체번호를 잃는가.
//  §2 읽기 실패 덤프가 와이어에 남기는 것(PC 의 누적 버퍼를 비울 근거가 있는가) + 다음 스코프 덤프의 블록 집합.
//  §3 이어 붙이기 drop-front 의 산술(add==1 에서 chunk[1] 이 새로 온 바이트가 아니다).
#include "common.h"

static SimCard a, b;

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
static int gate_of(const SimCard &c)
{
    int n = 0;
    memcpy(&n, c.data[SECTOR1_GATEWAY], 2);
    return n;
}

int main()
{
    // ─────────────────── §1 게이트웨이: 'Z' + 온전한 전문의 G1 본체번호 ───────────────────
    rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
    boot('G');
    {
        // (1a) 대조 — 앞바이트 없음. G1=7 이 태그의 게이트웨이 블록에 적혀야 한다.
        const char p1[] = "G17;G22026;9;23;3;11;15;0;G3PT0001;NAMEA;;G4SUBJA;;;G5;";
        serial_inject(p1, sizeof(p1) - 1);
        pump(3000);
        fresh_scope(a, 0x70, 70);
        logs_clear();
        touch(a);
        tlog("  (1a) 앞바이트 없음 -> 태그 본체번호=%d\n", gate_of(a));
        CHECK(gate_of(a) == 7, "(1a) 대조: 앞바이트 없는 전문의 G1 본체번호 7 이 기록된다");
    }
    {
        // (1b) 세척관리의 30초 keepalive 'Z' 가 같은 버퍼에 붙은 경우(AA2 P1-1 이 다루는 실제 경우).
        //      여기서 본체번호를 잃으면 부팅 뒤 첫 환자의 게이트웨이 번호가 기기 자체 번호로 떨어진다.
        hard_reset(false, 2);                       // mGateNumber 를 -1 로 되돌린다(부팅 뒤 첫 전문)
        deviceOption.SetType('G');
        sim_advance_ms(60UL * 1000);
        const char p2[] = "ZG17;G22026;9;23;3;12;15;0;G3PT0002;NAMEB;;G4SUBJB;;;G5;";
        serial_inject(p2, sizeof(p2) - 1);
        pump(3000);
        fresh_scope(b, 0x71, 71);
        logs_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  (1b) 'Z'+전문 -> 태그 본체번호=%d 환자키=%.8s %02u:%02u\n", gate_of(b),
             (const char *)b.data[SECTOR2_PATIENT_KEY], d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "PT0002", 6) == 0,
              "(1b) 전제: 'Z' 가 붙은 전문도 그 환자로 기록된다(AA2 P1-1 유지)");
        CHECK(gate_of(b) == 7, "(1b) 'Z' 가 붙어도 G1 본체번호 7 이 살아 있다");
    }

    // ─────────────────── §2 서버 덤프: Ok! 를 안 낼 때 와이어에 남는 것 ───────────────────
    deviceOption.SetType('S');
    hard_reset(false, 2);
    rtc_set(DateTime(2026, 9, 23, 10, 0, 0));
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    {
        // 스코프 A — 이동 소독(소독 2회)까지 끝난 태그. 덤프 **후반**(블록 10)에서 읽기 실패.
        make_tag(a, 0x81, SCOPE_TYPE_TAG, 81, "SC0081", "S0081");
        set_process(a, Process{1, 2, 1, 1, 1, true, 0, 2});
        set_record(a, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 23, 9, 0, 0));
        set_record(a, SECTOR3_WASHING_END, 1, DateTime(2026, 9, 23, 9, 4, 0));
        set_record(a, SECTOR5_DISINFECTION_START, 2, DateTime(2026, 9, 23, 9, 5, 0));
        set_record(a, SECTOR6_DISINFECTION_END, 2, DateTime(2026, 9, 23, 9, 23, 0));
        set_record(a, SECTOR7_DISINFECTION_START, 9, DateTime(2026, 9, 23, 9, 30, 0));
        set_record(a, SECTOR8_DISINFECTION_END, 9, DateTime(2026, 9, 23, 9, 50, 0));
        memcpy(a.data[SECTOR15_EXAMINATION_SUBJECT], "EGD", 4);
        a.readErrBlock = SECTOR2_WASHING_START;     // 'W;' 절 — 소독 1·2차와 섹터15 는 이미 나간 뒤
        a.readErrTimes = 5;
        logs_clear();
        serial_inject("Z", 1);
        touch(a);
        const bool s2start = serial_has("1F1C");
        const bool s2end   = serial_has("2320");
        const bool s2zero  = serial_has("1F1C00000000000000000000000000000000;");
        const bool subj    = serial_has("3F3C454744");     // 3F3C + "EGD"
        tlog("  (2a) A 실패 덤프: Ok!=%d 1F1C=%d(0채움=%d) 2320=%d 검사항목=%d\n",
             serial_has("Ok!"), s2start, s2zero, s2end, subj);
        tlog("       거부문자열: NotWD=%d NotWash=%d NotDis=%d NotPat=%d Sm!=%d ReadErrorEcho=%d\n",
             serial_has("Not W and D"), serial_has("Not Washing"), serial_has("Not Disinfection"),
             serial_has("Not Patient Info"), serial_has("Sm!"), serial_has("Read Error"));
        CHECK(!serial_has("Ok!") && lcd_has("Read Error"), "(2a) 전제: 실패 덤프는 Ok! 없이 LCD 경고만");
        CHECK(s2start && s2end && !s2zero, "(2a) 전제: 2차 소독 블록(1F1C·2320)이 **실값으로** 이미 나갔다");
        CHECK(subj, "(2a) 전제: 검사항목(3F3C)도 실값으로 이미 나갔다");
        CHECK(!serial_has("Not W and D") && !serial_has("Not Washing") && !serial_has("Not Disinfection") &&
              !serial_has("Not Patient Info") && !serial_has("Sm!") && !serial_has("Read Error"),
              "(2a) 실패 덤프의 와이어에 PC 가 누적 버퍼를 비우는 문자열이 하나도 없다");
    }
    {
        // 스코프 B — 이동 없음(소독 1회) · 환자정보 없음. 온전히 덤프된다.
        make_tag(b, 0x82, SCOPE_TYPE_TAG, 82, "SC0082", "S0082");
        set_process(b, Process{0, 1, 1, 1, 1, false, 0, 2});
        set_record(b, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 23, 9, 10, 0));
        set_record(b, SECTOR3_WASHING_END, 1, DateTime(2026, 9, 23, 9, 14, 0));
        set_record(b, SECTOR5_DISINFECTION_START, 3, DateTime(2026, 9, 23, 9, 15, 0));
        set_record(b, SECTOR6_DISINFECTION_END, 3, DateTime(2026, 9, 23, 9, 33, 0));
        logs_clear();
        serial_inject("Z", 1);
        touch(b);
        tlog("  (2b) B 온전 덤프: Ok!=%d 1F1C=%d 2320=%d 검사항목비어있음=%d\n", serial_has("Ok!"),
             serial_has("1F1C"), serial_has("2320"),
             serial_has("3F3C00000000000000000000000000000000;"));
        CHECK(serial_has("Ok!"), "(2b) 전제: B 의 덤프는 Ok! 로 끝난다(PC 가 이때 저장한다)");
        CHECK(!serial_has("1F1C") && !serial_has("2320"),
              "(2b) B(소독 1회)의 덤프에는 2차 소독 블록 줄이 **아예 없다** — A 의 잔재를 덮을 줄이 없다");
        CHECK(serial_has("3F3C00000000000000000000000000000000;"),
              "(2b) B 의 검사항목 줄은 0 이다 — PC 는 빈 값을 버리므로 A 의 검사항목이 남는다");
    }

    // ─────────────────── §3 drop-front 산술: add==1 에서 chunk[1] ───────────────────
    deviceOption.SetType('G');
    hard_reset(false, 2);
    rtc_set(DateTime(2026, 9, 23, 11, 0, 0));
    {
        // 조각 ①: 꼬리 없는 레코드(머리 G1) → 기다린다.
        // 조각 ②: 새 레코드 머리 "G1…"(꼬리 없음) → drop-front 가 앞을 버린다. 그 뒤 버퍼의
        //          [len+1] 자리에는 ① 의 잔재가 남는다.
        // 조각 ③: 'G' 한 바이트만 → chunk[1] 은 새로 온 바이트가 아니라 ② 의 잔재를 읽는다.
        const uint32_t t0 = g_ms + 50;
        const char c1[] = "G10000;G22026;9;23;3;13;00;0;G3PT0003;NAMEC;;G4SUBJC;;;";
        const char c2[] = "G10000;G22026;9;23;3;14;00;0;G3PT0004;NAMED;;";
        const char c3[] = "G";
        serial_queue(c1, sizeof(c1) - 1, t0);
        serial_queue(c2, sizeof(c2) - 1, t0 + 1800);
        serial_queue(c3, sizeof(c3) - 1, t0 + 3800);
        pump(12000);
        fresh_scope(a, 0x73, 73);
        logs_clear();
        touch(a);
        tlog("  (3) 세 조각 뒤 환자키=[%.8s] Status=%u\n", (const char *)a.data[SECTOR2_PATIENT_KEY],
             get_process(a).Status);
        CHECK(a.data[SECTOR2_PATIENT_KEY][0] == 0,
              "(3) 미완 레코드 셋으로는 아무 환자도 기록되지 않는다");
    }

    done();
    for (;;) {}
}
