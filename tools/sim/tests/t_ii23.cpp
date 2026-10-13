// 이동 재확인의 세 창 — 경계 표본(19차 VII-H: 값을 바꿔도 초록이던 자리).
//  표지로 재확인한 뒤 2초 더(shorten) · 실패 표지 10초(kFailWindowMs) · 성공한 이동 뒤 섹터6 시각 2초(kReconfirmWindowSec).
//  스코프 조건은 t_gg2m ⑫j·⑫n·⑫q 가 잠근다.
#include "common.h"

static SimCard clr, a;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

// 기기 2 에서 1차 소독이 커밋된(RW=2) 스코프
static void started_at2(SimCard &c, uint8_t uid, int no)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(c, Process{1, 1, 1, 1, 2, false, 0, 2});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    set_record(c, SECTOR5_DISINFECTION_START, 2, rel_date(9, 10, 0));
    DisinfectionDetail d{};
    d.GroupNumber = 1;
    put_block(c, SECTOR14_DISINFECTION_DETAIL, &d, sizeof(d));
}
// 새 기기 상태 + 액교환(이동 표시 · 횟수 0)
static void fresh_cleared(uint8_t minute)
{
    hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetMaximumCount(0); disinfectionOption.SetCount(9);
    rtc_set(rel_date(21, minute, 0)); touch(clr);
    sim_advance_ms(3000);
}
// 이동 커밋은 닿고 확인 읽기가 끊긴다 → Write Error + 표지
static bool torn_move(SimCard &c)
{
    c.readErrBlock = SECTOR1_PROCESS; c.readErrSkip = 1; c.readErrTimes = 30;
    logs_clear(); buzz_clear(); touch(c, 1, 4);
    const bool werr = lcd_has("Write Error");
    c.readErrBlock = -1; c.readErrTimes = 0; c.readErrSkip = 0;
    return werr;
}
// 접촉 뒤: 'R' 재확인(태그 불변·성공음·횟수 0) · '2' 2차 시작(횟수 1) · '?' 그 밖
static char kind_after(SimCard &c)
{
    logs_clear(); buzz_clear(); touch(c);
    const Process p = get_process(c);
    if (p.MovementNeeded && p.Rewrite == 0 && p.DisinfectionCount == 1 && buzz_count(50) == 1 && disinfectionOption.GetCount() == 0) return 'R';
    if (p.Rewrite == 2 && p.DisinfectionCount == 2 && disinfectionOption.GetCount() == 1) return '2';
    return '?';
}

int main()
{
    rtc_set(rel_date(9, 30, 0));
    boot('D');
    make_tag(clr, 0x0A, CLEAR_TYPE_TAG, 0, "CLR", "CLR");

    // ── ①② 표지로 재확인한 뒤 "2초 더": 1.5초 뒤 한 번 더 = 재확인 · 2.6초 뒤 = 2차 시작 ──
    for (uint8_t k = 0; k < 2; ++k)
    {
        fresh_cleared(30 + k);
        started_at2(a, (uint8_t)(0xA3 + k), 63 + k);
        const bool werr = torn_move(a);
        sim_advance_ms(2500);
        const uint32_t tA = g_ms;
        const char first = kind_after(a);                                   // 재확인(표지)
        const uint32_t wait = k ? 2600UL : 1500UL;
        if (g_ms - tA < wait) sim_advance_ms(wait - (g_ms - tA));
        const char again = kind_after(a);
        tlog("  %s WE=%d 재확인=%c → %lums 뒤=%c\n", k ? "②" : "①", werr, first, (unsigned long)wait, again);
        if (k == 0)
            CHECK(werr && first == 'R' && again == 'R', "① 표지 재확인 1.5초 뒤 한 번 더 읽혀도 재확인(2초 더 산다 — 1초면 2차 시작이 된다)");
        else
            CHECK(werr && first == 'R' && again == '2', "② 표지 재확인 2.6초 뒤 접촉은 2차 시작(2초만 더 — 3초면 되넣기를 삼킨다)");
    }
    // ── ③④ 실패 표지 10초: 실패 접촉이 끝난 뒤 9.0초 = 재확인 · 10.1초 = 2차 시작(표지는 실패음 뒤에 서서 접촉 끝보다 0~0.3초 앞) ──
    for (uint8_t k = 0; k < 2; ++k)
    {
        fresh_cleared(40 + k);
        started_at2(a, (uint8_t)(0xA5 + k), 65 + k);
        const bool werr = torn_move(a);
        sim_advance_ms(k ? 10100UL : 9000UL);
        const char got = kind_after(a);
        tlog("  %s WE=%d 실패 접촉 끝 %s 뒤=%c\n", k ? "④" : "③", werr, k ? "10.1초" : "9.0초", got);
        if (k == 0)
            CHECK(werr && got == 'R', "③ 찢긴 이동 9초 뒤 재접촉은 아직 재확인(표지 10초 창 안)");
        else
            CHECK(werr && got == '2', "④ 찢긴 이동 10.1초 뒤 재접촉은 2차 시작(표지 10초 창 밖)");
    }
    // ── ⑤⑥ 성공한 이동 뒤 섹터6 시각 2초: 태그 시각 +1초 = 재확인(튐) · +2초 = 2차 시작(창은 2초 '안') ──
    for (uint8_t k = 0; k < 2; ++k)
    {
        fresh_cleared(50 + k);
        started_at2(a, (uint8_t)(0xA8 + k), 68 + k);
        logs_clear(); buzz_clear(); touch(a);                               // 이동(성공) — 섹터6 에 종료 시각
        const Process pm = get_process(a);
        const bool moved = pm.MovementNeeded && pm.Rewrite == 0;
        rtc_set(DateTime(DefaultRtc::ToDateTime(get_ldt(a, SECTOR6_DISINFECTION_END)).unixtime() + 1 + k));
        const char got = kind_after(a);
        tlog("  %s 이동=%d 태그 시각 +%u초=%c\n", k ? "⑥" : "⑤", moved, 1 + k, got);
        if (k == 0)
            CHECK(moved && got == 'R', "⑤ 성공한 이동 뒤 태그 시각 +1초 재접촉은 재확인(튐)");
        else
            CHECK(moved && got == '2', "⑥ 성공한 이동 뒤 태그 시각 +2초 되넣기는 2차 시작(차이 2 는 창 밖)");
    }
    done();
    for (;;) {}
}
