// GG2 ③-보 — 액교환 뒤 **욕조 스코프 여럿을 차례로** 이동시키는 연속 처리에서, 한 번의 실수 접촉이
// 남은 스코프 전부에 번지는가. (FF2 P1-1 은 방아쇠로 재부팅·거부만 들었다 — 여기선 '연속 처리 중 재접촉'.)
#include "common.h"

static SimCard clr, a, b;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};

// 기기 2 에서 1차 소독이 커밋된(RW=2) 스코프
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
static void tlog_p(const char *tag, const SimCard &c)
{
    const Process p = get_process(c);
    tlog("  %s MV=%u RW=%u DC=%u MN=%d\n", tag, p.MovementNeeded, p.Rewrite,
         p.DisinfectionCount, p.MachineNumber);
}

int main()
{
    rtc_set(rel_date(9, 30, 0));
    boot('D');
    deviceOption.SetNumber(2);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    disinfectionOption.SetMaximumCount(0);
    disinfectionOption.SetCount(9);
    make_tag(clr, 0x0A, CLEAR_TYPE_TAG, 0, "CLR", "CLR");
    started_at2(a, 0x71, 71);
    started_at2(b, 0x72, 72);

    // ── 액교환: 클리어 태그 ──
    logs_clear(); touch(clr);
    tlog("  클리어: Clear표시=%d 횟수=%d\n", lcd_has("Clear"), disinfectionOption.GetCount());
    CHECK(lcd_has("Clear") && disinfectionOption.GetCount() == 0, "①양성대조: 클리어 태그로 횟수 0");

    // ── 욕조 스코프 A 를 이동 처리(정상) ──
    sim_advance_ms(5000);
    logs_clear(); touch(a);
    tlog_p("A이동", a);
    CHECK(get_process(a).MovementNeeded && get_process(a).Rewrite == 0 && buzz_count(50) == 1,
          "②양성대조: A 가 이동으로 처리된다(MV=1·RW=0·성공음)");

    // ── ★실수: 방금 옮긴 A 를 출발 기기(2)에 한 번 더 댄다 ──
    sim_advance_ms(5000);
    logs_clear(); touch(a);
    const Process pa = get_process(a);
    tlog_p("A재접촉", a);
    tlog("  A재접촉: 성공음=%d 거부음=%d 실패음=%d 횟수=%d 섹터7시작기기=%d\n",
         buzz_count(50), buzz_count(700), buzz_count(100), disinfectionOption.GetCount(),
         (int)(a.data[SECTOR7_DISINFECTION_START][0] | (a.data[SECTOR7_DISINFECTION_START][1] << 8)));
    CHECK(pa.Rewrite == 2 && pa.DisinfectionCount == 2 && buzz_count(50) == 1,
          "★③ 방금 옮긴 스코프를 출발 기기에 다시 대면 **2차 소독 시작**이 출발 기기로 기록되고 성공음이 난다");
    CHECK(disinfectionOption.GetCount() == 1,
          "★④ 액교환 직후인데 출발 기기 소독 횟수가 1 로 오른다(그 액으로 소독하지 않았다)");

    // ── 그 뒤 욕조에 남은 스코프 B — 이동이 아니라 '종료' 가 되는가 ──
    sim_advance_ms(5000);
    logs_clear(); touch(b);
    const Process pb = get_process(b);
    tlog_p("B", b);
    const LocalDateTime be = get_ldt(b, SECTOR6_DISINFECTION_END);
    tlog("  B: 성공음=%d 종료블록24기기=%d %02u:%02u:%02u\n", buzz_count(50),
         (int)(b.data[SECTOR6_DISINFECTION_END][0] | (b.data[SECTOR6_DISINFECTION_END][1] << 8)),
         be.Time.Hour, be.Time.Minute, be.Time.Second);
    // ★내 판정식 오류(기록): 처음엔 RW==0 을 기대했는데 **정상 종료도 Process 를 쓰지 않는다**
    //  (disinfection_end 는 블록 25·26·24 만 쓴다) — RW=2 는 서버 덤프가 지운다. 그래서 종료 기록 자체로 가른다.
    // 12차 봉합: 이미 이동한 스코프를 다시 댄 것은 '새 시작' 이 아니므로 이동 플래그를 내리지 않는다 →
    //  욕조에 남은 B 는 그대로 **이동**으로 처리된다(MV=1). 종전엔 조용히 1차 종료가 되어 2차 소독이 유실됐다.
    CHECK(pb.MovementNeeded == true,
          "★⑤ 실수 접촉이 있어도 욕조에 남은 B 는 이동으로 처리된다(2차 소독 유실 없음)");

    // ── 옮긴 A 를 도착 기기(3)에서 종료하려면 ──
    deviceOption.SetNumber(3);
    sim_advance_ms(5000);
    logs_clear(); touch(a);
    tlog("  A@기기3: OtherMachine=%d 거부음=%d 종료블록32기기=%d\n", lcd_has("Other Machine"),
         buzz_count(700), (int)(a.data[SECTOR8_DISINFECTION_END][0] | (a.data[SECTOR8_DISINFECTION_END][1] << 8)));
    // 12차 봉합: 이동 플래그가 살아 있으므로 A 도 도착 기기에서 처리된다 — 출발 기기로 되가져갈 필요가 없다.
    //  (실수 접촉으로 2차 시작이 출발 기기로 적히는 것 자체는 그대로 · 5차 판정 ③ "조작 오류 범위")
    CHECK(!lcd_has("Other Machine"),
          "★⑥ 그 A 도 도착 기기(3)에서 처리된다(거부로 막히지 않는다)");

    // ── 대조: 실수 접촉이 없으면 B 도 정상 이동된다 ──
    hard_reset(false, 2);
    deviceOption.SetNumber(2);
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    disinfectionOption.SetCount(9);
    started_at2(a, 0x73, 73);
    started_at2(b, 0x74, 74);
    logs_clear(); touch(clr);
    sim_advance_ms(5000); logs_clear(); touch(a);
    sim_advance_ms(5000); logs_clear(); touch(b);
    tlog_p("대조A", a); tlog_p("대조B", b);
    CHECK(get_process(a).MovementNeeded && get_process(b).MovementNeeded &&
          disinfectionOption.GetCount() == 0,
          "⑦대조: 실수 접촉이 없으면 A·B 둘 다 이동되고 출발 기기 횟수는 0 그대로");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
