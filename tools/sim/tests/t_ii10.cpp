// II-H 제안 잠금 3 — HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  E) v2.2.13(5차 B P2-1) Poll 의 Invalid 가 present/prev 를 지운다 — t_a7 B P2-1 은 **다른 UID** 스코프를 써서
//     v2.2.16 의 sameAsLast 판정만으로도 Connected 가 된다(헛초록). 남은 피해는 **직전에 처리한 그 스코프**를
//     비지원 카드와 바꿔 댈 때다(같은 UID → KeepAlive 로 무음).
//  F) v2.2.13(5차 C P2-①) 일회성 담당자 소모 — 소독기 쪽 consume_disposability() 를 지워도 772/772 초록이었다
//     (t_gg2w 는 세척기만 본다). 일회성 ON 소독기: 한 번 등록으로 두 스코프가 시작되면 안 된다.
#include "common.h"

static SimCard mgr, x, bad, y;

static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}

int main()
{
    // ── E) 직전에 처리한 스코프 X → (뗌) → 비지원 카드 → 치우지 않고 X 로 교체 ──
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);
    {
        fresh_scope(x, 0x21, 21);
        touch(x);                                        // X 세척 시작(마지막 Connected UID = X)
        CHECK(get_process(x).Rewrite == 1, "E 전제: X 세척 시작");
        sim_advance_ms(10UL * 60 * 1000);                // 10분 뒤(더블터치 창 밖)
        run_loops(2);
        card_init_foreign(bad, 0x09);
        bad.sak = 0x00;                                  // 비 MIFARE → Invalid
        card_place(&bad);
        run_loops(1);
        logs_clear();
        card_place(&x);                                  // 빈 폴링 없이 X 로 교체(같이 있다가 X 가 잡힘)
        run_loops(3);
        card_remove(); run_loops(4);
        const LocalDateTime e = get_ldt(x, SECTOR3_WASHING_END);
        tlog("  E 비지원 뒤 같은 X: 종료=%02u:%02u (자동종료 10:04 · 실제 10:10)\n", e.Time.Hour, e.Time.Minute);
        CHECK(e.Time.Hour == 10 && e.Time.Minute == 10,
              "E 비지원 카드 뒤 곧바로 댄 **같은** 스코프도 처리된다(종료 10:10 기록 · KeepAlive 무음 아님)");
    }

    // ── F) 일회성 담당자 ON 소독기 — 한 번 등록으로 두 스코프를 시작하지 않는다 ──
    {
        deviceOption.SetType('D');
        hard_reset(false, 2);
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                      // 일회성 충전
        washed_scope(x, 0x31, 31);
        touch(x);                                        // 첫 스코프 시작 → 소모
        const Process px = get_process(x);
        washed_scope(y, 0x32, 32);
        logs_clear();
        touch(y);                                        // 담당자 재등록 없이 둘째 스코프
        const Process py = get_process(y);
        tlog("  F 일회성: 첫 RW=%u · 둘째 RW=%u NoManager=%d\n", (unsigned)px.Rewrite, (unsigned)py.Rewrite,
             (int)lcd_has("No Manager Info"));
        CHECK(px.Rewrite == 2, "F 전제: 첫 스코프 소독 시작");
        CHECK(py.Rewrite == 0 && lcd_has("No Manager Info"),
              "F 일회성 담당자는 소독 시작 커밋에 소모된다 — 재등록 없이 둘째 스코프는 거부");
        recordOption.SetManagerDisposability(false);
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
