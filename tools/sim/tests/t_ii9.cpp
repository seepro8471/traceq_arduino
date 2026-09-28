// II-H 제안 잠금 2 — HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  C) v2.2.24 P2(CC1 P2-1) `IsGatewayFrame` — 'Z' 이외의 앞바이트(';')가 붙은 온전한 전문도 게이트웨이 전문이다.
//     `cmd[0]=='G'` 로 되돌려도 772/772 초록이었다(시험이 'Z' 앞붙음만 본다).
//  D) v2.2.30 잔재 판정의 `!rtc.IsUnsynced()` 관문 — 방전 시계끼리 비교해 이번 검사 정보를 지우지 않는다.
//     관문을 지워도 772/772 초록이었다(모든 잔재 시험이 동기된 시계에서만 돈다).
#include "common.h"

static SimCard sc, mgr;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

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

int main()
{
    // ── C) ';' 한 바이트가 앞에 붙은 온전한 전문(511 절단 뒷동이 딱 그 자리에서 시작하는 경우) ──
    rtc_set(rel_date(10, 0, 0));
    boot('G');
    {
        const char p[] = ";G22026;9;23;3;11;15;0;G3PT0077;NAMEZ;;G4SUBZ;;;G5;";
        serial_inject(p, sizeof(p) - 1);
        pump(3000);
        make_tag(sc, 0x51, SCOPE_TYPE_TAG, 51, "SC0051", "S0051");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        logs_clear();
        touch(sc);
        const LocalDateTime d = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  C ';'+전문 → 키=%.8s Status=%u %02u:%02u Sm!=%d\n", (const char *)sc.data[SECTOR2_PATIENT_KEY],
             (unsigned)get_process(sc).Status, d.Time.Hour, d.Time.Minute, (int)serial_has("Sm!"));
        CHECK(memcmp(sc.data[SECTOR2_PATIENT_KEY], "PT0077", 6) == 0 && get_process(sc).Status == 1 &&
              d.Time.Hour == 11 && d.Time.Minute == 15,
              "C 'Z' 가 아닌 앞바이트(';')가 붙은 온전한 전문도 그 환자로 기록된다(IsGatewayFrame)");
    }

    // ── D) 이 세척기 시계가 방전 표지(미동기)면 잔재 판정을 하지 않는다 ──
    {
        rtc_set(DateTime(2026, 1, 1, 0, 10, 0));            // 방전 표지 시각 = IsUnsynced
        deviceOption.SetType('W');
        hard_reset(false, 2);
        managerOption.SetData(mk, mn);
        make_tag(sc, 0x52, SCOPE_TYPE_TAG, 52, "SC0052", "S0052");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});   // 덤프 뒤(공정 0) — 잔재 판정이 도는 유일한 상태(14차)
        set_record(sc, SECTOR2_WASHING_START, 1, DateTime(2026, 1, 1, 0, 4, 0));
        set_record(sc, SECTOR3_WASHING_END, 1, DateTime(2026, 1, 1, 0, 5, 0));   // 기준 ≤ 지금(방전 시계끼리)
        set_record(sc, SECTOR1_GATEWAY, 7, DateTime(2026, 1, 1, 0, 1, 0));       // 검사 ≤ 기준
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "DEADCLK1", 8);
        logs_clear();
        touch(sc, 2, 4);
        const Process p = get_process(sc);
        tlog("  D 미동기 세척 시작: RW=%u 검사항목=%.8s 검사일시연도=%u\n", (unsigned)p.Rewrite,
             (const char *)sc.data[SECTOR15_EXAMINATION_SUBJECT], get_ldt(sc, SECTOR1_GATEWAY).Date.Year);
        CHECK(p.Rewrite == 1, "D 전제: 미동기 세척기에서도 세척 시작은 커밋된다");
        // [사장님 선택 1] 표지 규칙은 시계 상태와 무관 — 시간을 안 보니 미동기 세척기에서도 같은 판정이다.
        CHECK(sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0 && get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0,
              "D 미동기 세척기에서도 완료 뒤 Status 0 검사는 지운다(시간 비교 없음)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
