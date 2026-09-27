// 게이트웨이 조각 이어 붙이기가 **두 환자를 한 레코드로 섞지 않는가**.
// 앞 레코드가 꼬리 없이 끊긴 뒤 다음 레코드가 붙으면, 좁히기가 앞 레코드를 잡아 칸마다 다른 레코드를 집으며
// 성공음과 함께 조용히 틀린 기록이 나갔다(CC1 P1-1). v2.2.21 은 이어 붙이기가 없어 옳았고 v2.2.23 이 깼다.
#include "common.h"

static SimCard a, b, c;

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

int main()
{
    rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
    boot('G');

    // (a) 뒤 레코드에 G1 이 없는 경우 — 저장소 시험이 실재로 다루는 올눈 조각 모델
    {
        const uint32_t t0 = g_ms + 50;
        const char a1[] = "G10000;G22026;9;23;4;11;15;0;G3AAAAA;NAMEA;;";
        const char a2[] = "G22026;9;23;4;12;30;0;G3BBBBB;NAMEB;;G4SUBJB;;;G5;";
        serial_queue(a1, sizeof(a1) - 1, t0);
        serial_queue(a2, sizeof(a2) - 1, t0 + 1800);
        pump(8000);
        fresh_scope(a, 0x60, 60);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog("  (a) 키=%.8s 이름=%.8s 항목=%.8s %02u:%02u\n", (const char *)a.data[SECTOR2_PATIENT_KEY],
             (const char *)a.data[SECTOR2_PATIENT_NAME], (const char *)a.data[SECTOR15_EXAMINATION_SUBJECT],
             d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "BBBBB", 5) == 0 &&
              memcmp(a.data[SECTOR2_PATIENT_NAME], "NAMEB", 5) == 0 &&
              memcmp(a.data[SECTOR15_EXAMINATION_SUBJECT], "SUBJB", 5) == 0 &&
              ldt_eq(d, 2026, 9, 23, 12, 30, 0),
              "P1-1(a) G1 없는 레코드가 붙어도 한 환자 한 벌(뒤 환자)로 기록된다");
    }

    // (b) 앞 레코드가 값 한가운데에서 끊긴 경우 + 뒤는 온전한 레코드
    {
        sim_advance_ms(60UL * 1000);
        const uint32_t t1 = g_ms + 50;
        const char b1[] = "G10000;G22026;9;23;4;10;05;0;G3CCC";
        const char b2[] = "G10000;G22026;9;23;4;13;45;0;G3DDDDD;NAMED;;G4SUBJD;;;G5;";
        serial_queue(b1, sizeof(b1) - 1, t1);
        serial_queue(b2, sizeof(b2) - 1, t1 + 1800);
        pump(8000);
        fresh_scope(b, 0x61, 61);
        logs_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  (b) 키=%.10s 이름=%.8s %02u:%02u\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             (const char *)b.data[SECTOR2_PATIENT_NAME], d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "DDDDD", 5) == 0 &&
              memcmp(b.data[SECTOR2_PATIENT_NAME], "NAMED", 5) == 0 && ldt_eq(d, 2026, 9, 23, 13, 45, 0),
              "P1-1(b) 값 한가운데 절단 뒤 온전한 레코드가 붙어도 쓰레기가 섞이지 않는다");
    }

    // (c) 대조 — 'G1 만 늦게 온' 같은 레코드는 버리지 않는다(버리면 본체번호를 잃는다)
    {
        sim_advance_ms(60UL * 1000);
        const uint32_t t2 = g_ms + 50;
        const char c1[] = "G19999;";
        const char c2[] = "G22026;9;23;4;14;20;0;G3EEEEE;NAMEE;;G4SUBJE;;;G5;";
        serial_queue(c1, sizeof(c1) - 1, t2);
        serial_queue(c2, sizeof(c2) - 1, t2 + 1800);
        pump(8000);
        fresh_scope(c, 0x62, 62);
        ui.InvalidateHome();
        logs_clear();
        run_loops(1);
        touch(c);
        tlog("  (c) 키=%.8s 본체번호표시=%d\n", (const char *)c.data[SECTOR2_PATIENT_KEY], lcd_has(" 9999"));
        CHECK(memcmp(c.data[SECTOR2_PATIENT_KEY], "EEEEE", 5) == 0,
              "P1-1(c) 대조: G1 만 늦게 온 같은 레코드는 버리지 않는다");
        CHECK(lcd_has(" 9999"), "P1-1(c) 대조: 늦게 온 G1 의 본체번호가 살아 있다");
    }

    // (d) 앞 레코드가 **온전**하고 뒤 레코드가 쪼개진 경우 — 꼬리를 버퍼 전체에서 보면 앞의 G5 에 속아
    //     뒤 레코드를 기다리지 않고 둘 다 잃는다(CC1 P3-2). 꼬리는 마지막 머리 **뒤에서** 봐야 한다.
    {
        sim_advance_ms(60UL * 1000);
        const uint32_t t3 = g_ms + 50;
        const char d1[] = "G10000;G22026;9;23;4;15;10;0;G3FFFFF;NAMEF;;G4SUBJF;;;G5;G10000;G22026;9;23;4;16;25;0;";
        const char d2[] = "G3GGGGG;NAMEG;;G4SUBJG;;;G5;";
        serial_queue(d1, sizeof(d1) - 1, t3);
        serial_queue(d2, sizeof(d2) - 1, t3 + 1800);
        pump(8000);
        fresh_scope(a, 0x63, 63);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog("  (d) 키=%.8s %02u:%02u\n", (const char *)a.data[SECTOR2_PATIENT_KEY], d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "GGGGG", 5) == 0 && ldt_eq(d, 2026, 9, 23, 16, 25, 0),
              "P1-1(d) 앞 레코드가 온전해도 뒤 레코드가 쪼개지면 기다려 이어 붙인다");
    }

    done();
    for (;;) {}
}
