// DD1b — drop-front 의 모호성과 NeedsMoreBytes 경계.
//  §4 값이 "G1…" 로 시작하는 자리에서 이음매가 벌어지면 drop-front 가 **같은 레코드의 앞부분**을 버린다.
//     (v2.2.23 은 find_marker 의 경계 규칙 때문에 그 G1 을 마커로 보지 않아 온전히 기록했다.)
//  §5 머리(G1)가 버퍼 맨 끝에 걸릴 때 NeedsMoreBytes 가 기다리고, 이어 온 조각(머리 아님)이 붙는다.
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

int main()
{
    rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
    boot('G');

    // ── §4 환자 등록번호가 "G1234" 이고 그 바로 앞에서 이음매가 1초를 넘는다 ──
    {
        const uint32_t t0 = g_ms + 50;
        const char p1[] = "G10000;G22026;9;23;3;16;40;0;G3";
        const char p2[] = "G1234;NAMEX;;G4SUBJX;;;G5;";
        serial_queue(p1, sizeof(p1) - 1, t0);
        serial_queue(p2, sizeof(p2) - 1, t0 + 1800);
        pump(8000);
        fresh_scope(a, 0x75, 75);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog("  (4) 값이 G1 로 시작: 키=[%.8s] 이름=[%.8s] %02u:%02u Status=%u\n",
             (const char *)a.data[SECTOR2_PATIENT_KEY], (const char *)a.data[SECTOR2_PATIENT_NAME],
             d.Time.Hour, d.Time.Minute, get_process(a).Status);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "G1234", 5) == 0 &&
              memcmp(a.data[SECTOR2_PATIENT_NAME], "NAMEX", 5) == 0 && ldt_eq(d, 2026, 9, 23, 16, 40, 0),
              "(4) 등록번호가 'G1…' 인 환자가 이음매 뒤에 와도 온전히 기록된다");
    }

    // ── §5 머리(G1)가 버퍼 맨 끝에 걸린 경우 ──
    {
        sim_advance_ms(60UL * 1000);
        const uint32_t t1 = g_ms + 50;
        const char q1[] = "G10000;G22026;9;23;3;17;05;0;G3PT0005;NAMEF;;G4SUBJF;;;G5;G1";
        const char q2[] = "0000;G22026;9;23;3;18;25;0;G3PT0006;NAMEG;;G4SUBJG;;;G5;";
        serial_queue(q1, sizeof(q1) - 1, t1);
        serial_queue(q2, sizeof(q2) - 1, t1 + 1800);
        pump(8000);
        fresh_scope(b, 0x76, 76);
        logs_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog("  (5) 머리가 끝에 걸림: 키=[%.8s] %02u:%02u\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "PT0006", 6) == 0 && ldt_eq(d, 2026, 9, 23, 18, 25, 0),
              "(5) 'G1' 이 버퍼 끝에 걸려도 기다려 이어 붙이고 뒤 환자로 기록된다");
    }

    done();
    for (;;) {}
}
