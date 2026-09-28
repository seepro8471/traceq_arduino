// II-H 제안 잠금 4 — 접촉 예산(카드 동작 수)으로만 드러나는 두 봉합. HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  G) v2.2.29 "Status==2 는 잔재 판정을 건너뛴다"(update_process 가 같은 블록을 지운다 — 중복이면 예산 초과).
//     t_hh1b 8c 는 잔재 소거 동작을 Status 0·1·3·200 에서만 재고 **2 는 안 잰다** → 건너뛰기를 지워도 초록.
//  H) v2.2.30 "검사일시 0 이면 블록14 를 읽지도 않는다"(접촉 예산). t_ops 상한 40 이 34→36 을 못 가른다.
#include "common.h"

static SimCard mgr, sc;

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);

    // ── G) Status=2(1.0 전환 태그) + 지난 주기 잔재(검사 ≤ 지난 세척 종료) ──
    {
        make_tag(sc, 0x41, SCOPE_TYPE_TAG, 41, "SC0041", "S0041");
        set_process(sc, Process{2, 0, 0, 0, 0, false, 0, 0});   // 덤프 뒤(공정 0) + Status 2 — 잔재 판정 갈래에 닿는 상태(14차)
        set_record(sc, SECTOR2_WASHING_START, 1, yday(9, 0));
        set_record(sc, SECTOR3_WASHING_END, 1, yday(9, 4));
        set_record(sc, SECTOR1_GATEWAY, 7, yday(8, 0));
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUB22", 8);
        sc.opCount = 0;
        touch(sc, 2, 4);
        tlog("  G Status=2 + 잔재 → 동작 %u · RW=%u 검사항목비움=%d\n", (unsigned)sc.opCount,
             (unsigned)get_process(sc).Rewrite, (int)(sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0));
        CHECK(get_process(sc).Rewrite == 1 && sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0,
              "G 전제: Status=2 세척 시작이 커밋되고 검사항목이 비워진다(update_process)");
        CHECK(sc.opCount <= 45,
              "G Status=2 는 잔재 판정을 건너뛴다 — 같은 블록을 두 번 지우지 않는다(t_ff1a Status2 상한 45)");
    }

    // ── H) 검사일시 0(게이트웨이를 안 거친 스코프)의 매일 세척 시작 = 34 동작 ──
    {
        make_tag(sc, 0x42, SCOPE_TYPE_TAG, 42, "SC0042", "S0042");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        sc.opCount = 0;
        touch(sc, 2, 4);
        tlog("  H 검사일시 0 세척 시작 → 동작 %u\n", (unsigned)sc.opCount);
        CHECK(get_process(sc).Rewrite == 1, "H 전제: 세척 시작 커밋");
        CHECK(sc.opCount <= 34,
              "H 검사일시가 0 이면 블록14(지난 세척 종료)를 읽지 않는다 — 매일 도는 세척 시작 34 동작");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
