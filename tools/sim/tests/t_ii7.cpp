// 14회차 II-C P2-4 잠금 — 12차 `!isMoved` 봉합의 형제 문(try_load_manager_data 거부)
// (line 27-31) still clears mMovable when the rejected tag is an already-moved scope.
// Disposability ON, flag not charged (the last start consumed it), liquid exchanged, bath scopes A,B:
//   A move (ok) -> A touched again by mistake -> "No Manager Info" (reject) -> mMovable=false
//   -> B is silently ended as 1st disinfection (success sound) instead of moved.
#include "common.h"

static SimCard mgr, clr, a, b;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

static void started_at2(SimCard &c, uint8_t uid, int no)
{
    char id[8];
    snprintf(id, sizeof(id), "SC%02d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, "SER");
    set_process(c, Process{1, 1, 1, 1, 2, false, 0, 2});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    set_record(c, SECTOR5_DISINFECTION_START, 2, rel_date(9, 10, 0));
    DisinfectionDetail d{};
    d.GroupNumber = 1;
    put_block(c, SECTOR14_DISINFECTION_DETAIL, &d, sizeof(d));
}
static void plog(const char *tag, const SimCard &c)
{
    const Process p = get_process(c);
    tlog("  %s MV=%u RW=%u DC=%u MN=%d\n", tag, p.MovementNeeded, p.Rewrite, p.DisinfectionCount, p.MachineNumber);
}

static bool run(bool disposable, const char *label)
{
    hard_reset(false, 2);
    deviceOption.SetNumber(2);
    recordOption.SetManagerDisposability(disposable);
    recordOption.SetPatientCheck(false);
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    disinfectionOption.SetMaximumCount(0);
    started_at2(a, 0x71, 71);
    started_at2(b, 0x72, 72);
    logs_clear(); touch(clr);
    sim_advance_ms(5000); logs_clear(); touch(a);          // A move
    plog("A move", a);
    sim_advance_ms(5000); logs_clear(); touch(a);          // mistaken 2nd touch of the moved scope
    plog("A retouch", a);
    tlog("  %s A retouch: NoManager=%d reject600=%u ok50=%u\n", label, lcd_has("No Manager Info"), buzz_count(600), buzz_count(50));
    sim_advance_ms(5000); logs_clear(); touch(b);
    plog("B", b);
    const LocalDateTime be = get_ldt(b, SECTOR6_DISINFECTION_END);
    tlog("  %s B: MV=%u ok50=%u end24=%02u:%02u:%02u\n", label, get_process(b).MovementNeeded, buzz_count(50),
         be.Time.Hour, be.Time.Minute, be.Time.Second);
    return get_process(b).MovementNeeded;
}

int main()
{
    rtc_set(rel_date(9, 30, 0));
    boot('D');
    make_tag(clr, 0x0A, CLEAR_TYPE_TAG, 0, "CLR", "CLR");
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    const bool off = run(false, "[disposability OFF]");
    CHECK(off, "대조(일회성 OFF · 12차 봉합): A 를 실수로 다시 대도 B 는 이동된다");
    const bool on = run(true, "[disposability ON ]");
    CHECK(on, "일회성 ON 에서도 A 재접촉의 No Manager Info 거부가 이동 표시를 내리지 않아 B 는 이동된다");

    // S5b: clear (liquid exchange) tag touched twice within ~1 s (lift and re-touch) - no double-touch guard
    {
        const int cc0 = disinfectionOption.GetClearCount();
        touch(clr, 1, 4);
        touch(clr, 1, 4);
        tlog("  S5b clear tag x2 (<2 s): clearCount %d -> %d\n", cc0, disinfectionOption.GetClearCount());
        // [14차 판정] 클리어 태그는 더블터치 보호가 없어 두 번 대면 +2 — 1.4.1 과 같은 설계이고 소비처는 JSON 통계뿐(II-A 판정 · II-C 는 사실로만). 잠그지 않는다.
    }
    done();
    for (;;) {}
}
