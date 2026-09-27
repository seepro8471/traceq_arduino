// EE1b — 9회차 ④ `HasMarker(chunk,"G2")` 가 **버퍼 끝을 넘어 잔재 바이트를 읽는다**.
//  이어 붙이기 중 drop-front(memmove)가 한 번 일어나면 그 뒤의 추가 읽기는 `buffer[len] == 0` 불변식을
//  깨뜨린다(옮긴 뒤 남은 뒤쪽 잔재가 새 데이터 바로 뒤에 그대로 있다). 새로 더한 `HasMarker(chunk,"G2")` 는
//  strstr 이라 그 잔재까지 훑어 **G1 관문을 스스로 무력화**한다 → 같은 레코드의 앞동을 또 버려 환자를 잃는다.
//  B1 조각3 이 조각1 보다 **짧을 때**(잔재 노출) 환자가 기록되나
//  B2 조각3 이 조각1 보다 **길 때**(잔재 없음) 환자가 기록되나 — 대조
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

// 조각 셋을 1.8초 간격으로 — 첫 읽기(C1) → 추가 읽기1(C2) → 추가 읽기2(C3)
static void three_chunks(const char *c1, const char *c2, const char *c3)
{
    const uint32_t t0 = g_ms + 50;
    serial_queue(c1, strlen(c1), t0);
    serial_queue(c2, strlen(c2), t0 + 1800);
    serial_queue(c3, strlen(c3), t0 + 3600);
    pump(12000);
}

int main()
{
    rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
    boot('G');

    // C1 = 미완 레코드(꼬리 G5 없음) 50바이트 · C2 = 새 레코드 머리(경계 G2 있음 → drop-front) 32바이트
    static const char C1[] = "G10001;G22026;9;23;3;09;00;0;G3PT0001;OLDNAME;;G4";
    static const char C2[] = "G10002;G22026;9;23;3;10;00;0;G3";

    // ── B1 조각3(25B) < 조각1(50B) → memmove 뒤 잔재가 `buffer[len]` 자리에 남는다 ──
    {
        static const char C3[] = "G1234;NAMEX;;G4SUBJ;;;G5;";        // 25B
        tlog("  길이: C1=%u C2=%u C3=%u\n", (unsigned)strlen(C1), (unsigned)strlen(C2), (unsigned)strlen(C3));
        three_chunks(C1, C2, C3);
        fresh_scope(a, 0x81, 81);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog("  B1 본체번호=%d(전문 G1=0002) 키=[%.8s] 이름=[%.8s] %02u:%02u Status=%u\n",
             gate_of(a), (const char *)a.data[SECTOR2_PATIENT_KEY],
             (const char *)a.data[SECTOR2_PATIENT_NAME],
             d.Time.Hour, d.Time.Minute, get_process(a).Status);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "G1234", 5) == 0 &&
              memcmp(a.data[SECTOR2_PATIENT_NAME], "NAMEX", 5) == 0,
              "B1 drop-front 뒤 이음매에서도 등록번호가 'G1…' 인 환자를 잃지 않는다");
        CHECK(gate_of(a) == 2, "B1 본체번호도 전문이 준 값(2)이다 — 잔재에서 뽑은 숫자가 아니다");
    }

    // ── B2 대조: 조각3(64B) > 조각1(50B) → `buffer[len]` 이 진짜 NUL 이라 잔재를 못 본다 ──
    {
        sim_advance_ms(60UL * 1000);
        static const char C3[] = "G1234;NAMEY;;G4SUBJECTAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA;;;G5;";
        tlog("  길이: C3b=%u\n", (unsigned)strlen(C3));
        three_chunks(C1, C2, C3);
        fresh_scope(b, 0x82, 82);
        logs_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  B2 본체번호=%d(전문 G1=0002) 키=[%.8s] 이름=[%.8s] %02u:%02u Status=%u\n",
             gate_of(b), (const char *)b.data[SECTOR2_PATIENT_KEY],
             (const char *)b.data[SECTOR2_PATIENT_NAME],
             d.Time.Hour, d.Time.Minute, get_process(b).Status);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "G1234", 5) == 0 &&
              memcmp(b.data[SECTOR2_PATIENT_NAME], "NAMEY", 5) == 0,
              "B2 대조: 잔재가 안 닿는 길이면 같은 이음매에서 환자가 온전히 기록된다");
        CHECK(gate_of(b) == 2, "B2 대조: 그 경우 본체번호도 전문이 준 값(2)이다");
    }

    // ── B3 ③ `memmove` 뒤 `cmd = buffer` — 맨 앞 'Z' + drop-front 이 겹칠 때 ──
    //    'Z' 를 건너뛴 포인터를 그대로 넘기면 버린 앞부분을 가리켜 새 머리의 'G' 를 한 글자 잘라 먹고
    //    본체번호를 기기 자체 번호로 떨어뜨린다.
    {
        sim_advance_ms(60UL * 1000);
        static SimCard c;
        static const char Z1[] = "ZG10001;G22026;9;23;3;09;00;0;G3";               // 'Z' + 미완 레코드
        static const char Z2[] = "G10007;G22026;9;23;3;11;00;0;G3PT0007;NAMEZ;;G4S;;;G5;";
        const uint32_t t0 = g_ms + 50;
        serial_queue(Z1, sizeof(Z1) - 1, t0);
        serial_queue(Z2, sizeof(Z2) - 1, t0 + 1800);
        pump(9000);
        fresh_scope(c, 0x83, 83);
        logs_clear();
        touch(c);
        const LocalDateTime d = get_ldt(c, SECTOR1_GATEWAY);
        tlog("  B3 본체번호=%d(전문 G1=0007 · 기기번호 %d) 키=[%.8s] %02u:%02u\n",
             gate_of(c), deviceOption.GetNumber(), (const char *)c.data[SECTOR2_PATIENT_KEY],
             d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(c.data[SECTOR2_PATIENT_KEY], "PT0007", 6) == 0,
              "B3 양성대조: 'Z' + 이음매 + drop-front 뒤에도 환자가 기록된다");
        CHECK(gate_of(c) == 7,
              "B3 drop-front 뒤 본체번호는 버린 레코드가 아니라 **새 머리**의 값(7)이다");
    }

    // ── B4 새 레코드의 G1 과 G2 가 **다른 조각**으로 갈라져 온다 · 앞동에 경계 G2 가 있는 경우 ──
    //    본체번호를 잃는 것은 의도된 대가 — **환자까지 잃는지**가 물음이다.
    {
        sim_advance_ms(60UL * 1000);
        static SimCard c;
        three_chunks("G10001;G22026;9;23;3;09;00;0;G3",              // 앞동(경계 G2 있음) · 미완
                     "G10008;",                                      // 새 레코드의 G1 칸만
                     "G22026;9;23;3;12;00;0;G3PT0008;NAMEQ;;G4S;;;G5;");
        fresh_scope(c, 0x84, 84);
        logs_clear();
        touch(c);
        const LocalDateTime d = get_ldt(c, SECTOR1_GATEWAY);
        tlog("  B4 본체번호=%d 키=[%.8s] %02u:%02u Status=%u\n", gate_of(c),
             (const char *)c.data[SECTOR2_PATIENT_KEY], d.Time.Hour, d.Time.Minute,
             get_process(c).Status);
        CHECK(memcmp(c.data[SECTOR2_PATIENT_KEY], "PT0008", 6) == 0 && get_process(c).Status == 1,
              "B4 G1·G2 가 갈라져 와도 **환자는** 잃지 않는다(본체번호만 폴백)");
    }

    // ── B5 같은 분할이지만 앞동에 경계 G2 가 **없는** 경우 — 본체번호까지 살아야 한다 ──
    {
        sim_advance_ms(60UL * 1000);
        static SimCard c;
        three_chunks("G10001;",                                      // 앞동에 G2 없음
                     "G10009;",
                     "G22026;9;23;3;13;00;0;G3PT0009;NAMER;;G4S;;;G5;");
        fresh_scope(c, 0x85, 85);
        logs_clear();
        touch(c);
        const LocalDateTime d = get_ldt(c, SECTOR1_GATEWAY);
        tlog("  B5 본체번호=%d 키=[%.8s] %02u:%02u\n", gate_of(c),
             (const char *)c.data[SECTOR2_PATIENT_KEY], d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(c.data[SECTOR2_PATIENT_KEY], "PT0009", 6) == 0,
              "B5 앞동에 G2 가 없으면 환자도 본체번호도 마지막 G1 레코드로 옳게 읽는다");
        CHECK(gate_of(c) == 9, "B5 그 경우 본체번호는 9(마지막 G1)다");
    }

    done();
    for (;;) {}
}
