// EE1d — 9회차 ⑤ `started_just_now(…, &startDt)` / `not_before_start(startDt, …)`.
//  호출자 셋 전수에서 시작 시각이 **채워진 채** 쓰이는가 · 재읽기를 없앤 뒤에도 보정이 듣는가.
//  D1 세척 종료 — 시계를 되돌려도 종료가 시작보다 앞서지 않는다(시작 블록 재읽기가 실패해도)
//  D2 이동 종료(disinfector_move · 서명이 바뀐 자리) — 같은 보정이 듣는다
//  D3 시작 블록이 무효(0)인 태그 — 보정은 꺼지되 **종료 기록은 잃지 않는다**
#include "common.h"

static SimCard mgr, clr, sc;

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
}

int main()
{
    rtc_set(rel_date(14, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── D1 세척 종료 ──
    {
        make_tag(sc, 0x95, SCOPE_TYPE_TAG, 95, "SC0095", "S0095");
        set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 1});          // 세척 시작됨(Rewrite=1)
        set_record(sc, SECTOR2_WASHING_START, 1, rel_date(14, 0, 0));
        set_record(sc, SECTOR3_WASHING_END,   1, rel_date(14, 4, 0));
        rtc_set(rel_date(9, 0, 0));                                    // 시계를 5시간 되돌렸다
        sim_advance_ms(5000);
        sc.readErrBlock = SECTOR2_WASHING_START;
        sc.readErrSkip = 1;                                            // 첫 읽기(더블터치 판정)만 통과
        sc.readErrTimes = 4;
        logs_clear();
        touch(sc);
        sc.readErrBlock = -1; sc.readErrTimes = 0; sc.readErrSkip = 0;
        const LocalDateTime we = get_ldt(sc, SECTOR3_WASHING_END);
        tlog("  D1 세척 종료 = %02u:%02u (시작 14:00 · 시계 09:00) lcd=[%.40s]\n",
             we.Time.Hour, we.Time.Minute, g_lcdLog);
        CHECK(we.Time.Hour == 14,
              "D1 세척 종료: 시작 블록 재읽기가 없어도 보정이 듣는다(종료 = 시작 14:00)");
    }

    // ── D2 이동 종료 (disinfector_move) ──
    {
        as_type('D');
        touch(mgr);
        rtc_set(rel_date(14, 0, 0));
        make_tag(sc, 0x96, SCOPE_TYPE_TAG, 96, "SC0096", "S0096");
        set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 0});          // 세척까지 끝남
        set_record(sc, SECTOR2_WASHING_START, 1, rel_date(13, 0, 0));
        set_record(sc, SECTOR3_WASHING_END,   1, rel_date(13, 4, 0));
        touch(sc);                                                     // 소독 시작 14:00
        const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
        CHECK(ds.Time.Hour == 14, "D2 전제: 소독 시작이 14:00 으로 기록됐다");

        make_tag(clr, 0x03, CLEAR_TYPE_TAG, 0, "", "");
        touch(clr);                                                    // 액교환 = 이동 플래그
        rtc_set(rel_date(9, 0, 0));                                    // 시계를 되돌렸다
        sim_advance_ms(5000);
        sc.readErrBlock = SECTOR5_DISINFECTION_START;
        sc.readErrSkip = 1; sc.readErrTimes = 4;
        logs_clear();
        touch(sc);                                                     // 이동 종료
        sc.readErrBlock = -1; sc.readErrTimes = 0; sc.readErrSkip = 0;
        const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
        const Process p = get_process(sc);
        tlog("  D2 이동 종료 = %02u:%02u · MV=%u RW=%u lcd=[%.40s]\n",
             de.Time.Hour, de.Time.Minute, (unsigned)p.MovementNeeded, (unsigned)p.Rewrite, g_lcdLog);
        CHECK(p.MovementNeeded == 1 && p.Rewrite == 0, "D2 전제: 이동 커밋이 됐다");
        CHECK(de.Time.Hour == 14,
              "D2 이동 종료도 같은 보정이 듣는다(종료 = 소독 시작 14:00)");
    }

    // ── D3 시작 블록이 무효(전부 0)인 태그의 종료 ──
    {
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(15, 0, 0));
        make_tag(sc, 0x97, SCOPE_TYPE_TAG, 97, "SC0097", "S0097");
        set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 1});
        put_block(sc, SECTOR2_WASHING_START, "", 0);                   // 시작 기록이 전부 0(무효 시각)
        logs_clear();
        touch(sc);
        const LocalDateTime we = get_ldt(sc, SECTOR3_WASHING_END);
        tlog("  D3 시작 무효 → 세척 종료 = %04u-%02u-%02u %02u:%02u lcd=[%.40s]\n",
             we.Date.Year, we.Date.Month, we.Date.Day, we.Time.Hour, we.Time.Minute, g_lcdLog);
        CHECK(we.Time.Hour == 15 && we.Date.Year == TRACEQ_RELEASE_YEAR,
              "D3 시작 시각이 무효면 보정 없이 현재 시각으로 종료를 기록한다(기록을 잃지 않는다)");
    }

    done();
    for (;;) {}
}
