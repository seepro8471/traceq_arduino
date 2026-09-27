// DD1d — 소독 종료의 not_before_start 보정이 **시작 블록 재읽기에 달려 있다**.
//  §8 카드는 살아 있고 그 블록 읽기만 실패하면(리더 쪽 오류 · 재시도까지 실패) 보정이 조용히 꺼지고
//     '종료 < 시작' 기록이 그대로 커밋된다. 형제(세척 종료)도 같은 구조다.
#include "common.h"

static SimCard mgr, sc;

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
}

int main()
{
    rtc_set(rel_date(14, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // 세척까지 끝난 스코프 — 소독 시작 14:00
    make_tag(sc, 0x91, SCOPE_TYPE_TAG, 91, "SC0091", "S0091");
    set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(13, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(13, 4, 0));
    touch(sc);
    const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
    tlog("  (8) 소독 시작 = %02u:%02u\n", ds.Time.Hour, ds.Time.Minute);
    CHECK(ds.Time.Hour == 14, "(8) 전제: 소독 시작이 14:00 으로 기록됐다");

    // 시계를 되돌린 뒤(설정기 JSON·메뉴·RTC 방전) 종료 접촉 — 단, 시작 블록 읽기가
    // **두 번째 읽기부터** 실패한다(더블터치 판정용 첫 읽기는 통과 → not_before_start 의 읽기만 실패).
    rtc_set(rel_date(9, 0, 0));
    sim_advance_ms(5000);
    sc.readErrBlock = SECTOR5_DISINFECTION_START;
    sc.readErrSkip = 1;      // 첫 읽기는 통과
    sc.readErrTimes = 4;     // 그 뒤 읽기와 재시도는 실패
    logs_clear();
    touch(sc);
    sc.readErrBlock = -1;
    sc.readErrTimes = 0;
    sc.readErrSkip = 0;
    const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
    const Process p = get_process(sc);
    tlog("  (8) 시작 블록 재읽기 실패 뒤 종료 = %02u:%02u (DC=%u RW=%u lcd=[%.40s])\n",
         de.Time.Hour, de.Time.Minute, p.DisinfectionCount, p.Rewrite, g_lcdLog);
    CHECK(de.Time.Hour == 14 || de.Time.Hour == 0,
          "(8) 시작 블록 재읽기가 실패해도 '종료 < 시작' 기록은 남지 않는다");

    done();
    for (;;) {}
}
