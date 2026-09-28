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

    // ── A→A: 액교환한 그 소독기에 A 를 되넣어 댄다 = **2차 시작**(사장님 09-28 · 정상 흐름 — 12차까지 '실수' 로 봤다) ──
    sim_advance_ms(5000);
    logs_clear(); touch(a);
    const Process pa = get_process(a);
    tlog_p("A재접촉", a);
    tlog("  A재접촉: 성공음=%d 거부음=%d 실패음=%d 횟수=%d 섹터7시작기기=%d\n",
         buzz_count(50), buzz_count(700), buzz_count(100), disinfectionOption.GetCount(),
         (int)(a.data[SECTOR7_DISINFECTION_START][0] | (a.data[SECTOR7_DISINFECTION_START][1] << 8)));
    CHECK(pa.Rewrite == 2 && pa.DisinfectionCount == 2 && buzz_count(50) == 1,
          "★③ 방금 옮긴 스코프를 같은 기기에 다시 대면 **2차 소독 시작**이 그 기기로 기록되고 성공음이 난다(A→A)");
    CHECK(disinfectionOption.GetCount() == 1,
          "★④ A→A 2차 시작은 새 액으로 하는 소독이라 횟수가 1 로 오른다");

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
          "★⑤ A 의 A→A 2차 시작 뒤에도 욕조에 남은 B 는 이동으로 처리된다(2차 소독 유실 없음)");

    // ── ★A→A 2차 종료: 이 기기의 이동 표시가 아직 켜져 있다(B 를 옮기느라) — 종료 접촉이 '이동' 이 되면 안 된다 ──
    //  종전(12차)엔 이동 경로가 받아 RW=0 이 됐고, 그 뒤 한 번만 더 대면 시작 경로(더블터치 가드 없음)가 2차 시작을
    //  지금 시각으로 다시 쓰고 종료는 미리채움·횟수 +1 이었다. 12차는 이 조합을 "두 기기에서 액교환" 으로 좁게 봤다.
    const LocalDateTime s7 = get_ldt(a, SECTOR7_DISINFECTION_START);
    rtc_set(rel_date(10, 0, 0));
    logs_clear(); buzz_clear(); touch(a);
    const Process pe = get_process(a);
    const LocalDateTime e8 = get_ldt(a, SECTOR8_DISINFECTION_END);
    tlog_p("A2차종료", a);
    tlog("  A2차종료: 성공음=%d OtherMachine=%d 횟수=%d 종료블록8 기기=%d %02u:%02u · 시작블록7 %02u:%02u→%02u:%02u\n",
         buzz_count(50), lcd_has("Other Machine"), disinfectionOption.GetCount(),
         (int)(a.data[SECTOR8_DISINFECTION_END][0] | (a.data[SECTOR8_DISINFECTION_END][1] << 8)),
         e8.Time.Hour, e8.Time.Minute, s7.Time.Hour, s7.Time.Minute,
         get_ldt(a, SECTOR7_DISINFECTION_START).Time.Hour, get_ldt(a, SECTOR7_DISINFECTION_START).Time.Minute);
    CHECK(pe.Rewrite == 2 && pe.MovementNeeded && pe.DisinfectionCount == 2 && buzz_count(50) == 1 &&
          !lcd_has("Other Machine") && e8.Time.Hour == 10 && e8.Time.Minute == 0 &&
          (int)(a.data[SECTOR8_DISINFECTION_END][0] | (a.data[SECTOR8_DISINFECTION_END][1] << 8)) == 2,
          "★⑧ A→A 2차 종료는 이동 표시가 켜져 있어도 **종료**다 — 블록8 = 기기2·실제 시각 · RW=2 그대로(이동 아님)");
    // 한 번 더(사람 손의 재접촉 · 2초 가드 밖) — 종료의 재기록일 뿐 시작을 다시 쓰거나 횟수를 올리지 않는다
    rtc_set(rel_date(10, 0, 30));
    logs_clear(); buzz_clear(); touch(a);
    const LocalDateTime s7b = get_ldt(a, SECTOR7_DISINFECTION_START);
    const LocalDateTime e8b = get_ldt(a, SECTOR8_DISINFECTION_END);
    tlog("  A재접촉2: 횟수=%d RW=%u 시작블록7 %02u:%02u:%02u 종료블록8 %02u:%02u:%02u\n",
         disinfectionOption.GetCount(), get_process(a).Rewrite,
         s7b.Time.Hour, s7b.Time.Minute, s7b.Time.Second, e8b.Time.Hour, e8b.Time.Minute, e8b.Time.Second);
    CHECK(disinfectionOption.GetCount() == 1 && get_process(a).Rewrite == 2 &&
          s7b.Time.Hour == s7.Time.Hour && s7b.Time.Minute == s7.Time.Minute && s7b.Time.Second == s7.Time.Second &&
          e8b.Time.Hour == 10 && e8b.Time.Minute == 0 && e8b.Time.Second == 30,
          "★⑨ 2차 종료 뒤 재접촉은 종료 시각만 다시 쓴다 — 2차 시작(블록7)·횟수는 그대로(종전: 시작이 지금으로 덮이고 횟수 2)");

    // ── 옮긴 A 를 **실제** 도착 기기(3)에서 — 다른 기기는 자기 RAM 을 쓴다(이동 표시 없음) ──
    //  ★12차는 `SetNumber(3)` 만으로 도착기를 흉내 내 **출발기의 RAM 이동 표시를 물려받은 가짜 도착기**를 봤고,
    //   그래서 "도착기에서 처리된다" 는 헛초록이었다(14차 II-G P2-2). 실제 도착기 = RAM 초기화 + 다른 번호.
    hard_reset(false, 2);
    deviceOption.SetNumber(3);
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    sim_advance_ms(5000);
    logs_clear(); buzz_clear(); touch(a);
    tlog("  A@실제기기3: OtherMachine=%d 거부음600=%u 성공음50=%u 2차시작기기=%d\n", lcd_has("Other Machine"),
         (unsigned)buzz_count(600), (unsigned)buzz_count(50),
         (int)(a.data[SECTOR7_DISINFECTION_START][0] | (a.data[SECTOR7_DISINFECTION_START][1] << 8)));
    // [14차 사장님 판정(09-28) · 재론 금지] 기기2 에서 2차를 한 A 는 다른 기기(3)에서 `Other Machine` 으로 거부된다(태그의
    //  소독 칸이 둘뿐이라 3차는 없다). "이미 이동함" 안내로 바꾸지 않는다 — 같은 문구·같은 거부음·태그 불변.
    CHECK(lcd_has("Other Machine") && buzz_count(50) == 0,
          "⑥ 기기2 에서 2차를 한 A 는 다른 기기(3)에서 Other Machine 으로 거부된다(사장님 판정: 문구 그대로 · 재론 금지)");

    // ── ★형제: 그 다른 기기(3)가 **액교환을 해 이동 표시가 켜져 있어도** A 는 거부된다(2차 종료를 기기3 으로 덮지 않는다) ──
    logs_clear(); touch(clr);
    sim_advance_ms(5000);
    logs_clear(); buzz_clear(); touch(a);
    tlog("  A@기기3(이동표시): OtherMachine=%d 성공음=%d 종료블록8 기기=%d MV=%u RW=%u\n", lcd_has("Other Machine"),
         buzz_count(50), (int)(a.data[SECTOR8_DISINFECTION_END][0] | (a.data[SECTOR8_DISINFECTION_END][1] << 8)),
         get_process(a).MovementNeeded, get_process(a).Rewrite);
    CHECK(lcd_has("Other Machine") && buzz_count(50) == 0 && get_process(a).Rewrite == 2 &&
          (int)(a.data[SECTOR8_DISINFECTION_END][0] | (a.data[SECTOR8_DISINFECTION_END][1] << 8)) == 2,
          "★⑩ 이동 표시가 켜진 다른 기기(3)도 이미 이동한 A 를 받지 않는다 — 블록8 은 기기2 그대로(조용한 덮어쓰기 금지)");

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
