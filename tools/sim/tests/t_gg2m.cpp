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

    // ── ⑪ (15차 사장님 A1) A→A 도중 **다른 태그가 거부**(세척 안 된 스코프)돼도 이동 표시는 남는다 — 꺼냄 = 이동 · 되넣음 = 2차 시작 ──
    //  종전은 거부가 표시를 내려 꺼냄·되넣음·끝남이 전부 1차 (재)종료(성공음)가 됐고 2차·알람·횟수가 없었다(III-C P1). 남는 방아쇠 = 재부팅·새 1차 시작.
    {
        hard_reset(false, 2);
        deviceOption.SetNumber(2);
        managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0);
        disinfectionOption.SetCount(9);
        started_at2(a, 0x75, 75);
        make_tag(b, 0x76, SCOPE_TYPE_TAG, 76, "SC76", "SER");            // 세척 안 된 스코프 → No Washing Info
        set_process(b, Process{0, 0, 0, 0, 0, false, 0, 0});
        rtc_set(rel_date(11, 0, 0));
        touch(clr);
        sim_advance_ms(3000); logs_clear(); touch(b);
        const bool rejected = lcd_has("No Washing Info");
        // 둘째 거부 갈래(is_valid==0 · 담당자 미등록)도 표시를 안 내린다 — 16차 IV-H: 이 갈래만 되돌려도 초록이었다
        managerOption.SetData(nullptr, nullptr);                             // 담당자 없음(HasData=false)
        sim_advance_ms(3000); logs_clear(); touch(b);
        const bool rejected0 = lcd_has("No Manager Info");
        managerOption.SetData(mk, mn);
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 꺼냄
        const Process pmv = get_process(a);
        tlog_p("⑪ 꺼냄", a);
        rtc_set(rel_date(11, 1, 0)); logs_clear(); touch(a);               // 되넣음
        const Process p2 = get_process(a);
        tlog_p("⑪ 되넣음", a);
        tlog("  ⑪ 거부(세척 안 됨)=%d 거부(담당자 없음)=%d 횟수=%d\n", rejected, rejected0, disinfectionOption.GetCount());
        CHECK(rejected && rejected0 && pmv.MovementNeeded && pmv.Rewrite == 0,
              "⑪(15차 A1) 액교환 뒤 다른 태그가 거부(세척 안 됨·담당자 없음)돼도 꺼냄 접촉은 이동이다(표시를 안 내린다 · 1.0 동일)");
        CHECK(p2.MovementNeeded && p2.Rewrite == 2 && p2.DisinfectionCount == 2 && disinfectionOption.GetCount() == 1,
              "⑪b 되넣음 접촉은 2차 시작이고 새 액 횟수가 1 로 오른다");
    }

    // ── ⑫ (15차 A2 · 16차 재설계) 이동 커밋은 닿았는데 확인 읽기가 끊겨 Write Error → 실패음 뒤 재접촉 = 이동 재확인 ──
    //  15차의 "섹터6 시각 2초 안" 은 실패음(0.8초)이 초 단위 창을 먼저 먹어 대부분 못 잡았다(16차 여섯 갈래 실측) → 판정은
    //  "이 스코프의 직전 접촉이 Write Error 로 끝났고 10초 안"(RAM 실패 표지). 재접촉 지연 1.2초·2초 × 초 위상 0·0.7 넷 전부 재확인.
    for (uint8_t k = 0; k < 4; ++k)
    {
        const uint16_t phase = (k & 1) ? 700 : 0;
        const uint16_t delay = (k < 2) ? 1200 : 2000;
        hard_reset(false, 2);
        deviceOption.SetNumber(2);
        managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0);
        disinfectionOption.SetCount(9);
        make_tag(a, 0x80 + k, SCOPE_TYPE_TAG, 80 + k, "SC80", "SER");      // 세척 끝난 스코프 → 이 기기에서 1차 시작(알람·host 가 실제로 선다)
        set_process(a, Process{1, 0, 1, 0, 1, false, 0, 1});
        set_record(a, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
        set_record(a, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
        rtc_set(rel_date(12, 0, 0)); touch(a);                              // 1차 시작 @2
        const bool alarmSet = rtc.HasAlarm(2);
        rtc_set(rel_date(12, 5, 0)); touch(clr);                            // 액교환(알람이 아직 안 울린 시각 — 뒷정리 잠금이 헛초록이 안 되게 · g4)
        sim_advance_ms(3000 + phase);
        a.readErrBlock = SECTOR1_PROCESS; a.readErrSkip = 1; a.readErrTimes = 30;   // 이동 커밋 확인 읽기 실패
        logs_clear(); buzz_clear(); touch(a, 1, 4);
        const bool werr = lcd_has("Write Error");
        const Process pm = get_process(a);
        a.readErrBlock = -1; a.readErrTimes = 0; a.readErrSkip = 0;
        sim_advance_ms(delay);
        logs_clear(); buzz_clear(); touch(a);                               // 규칙대로 재접촉
        const Process pr = get_process(a);
        const bool blk7Empty = get_ldt(a, SECTOR7_DISINFECTION_START).Date.Year == 0;
        tlog("  ⑫[%u] 위상=%u 지연=%u: WriteError=%d 커밋 MV=%u RW=%u → 재접촉 MV=%u RW=%u DC=%u 블록7비움=%d 성공음=%u 알람=%d 횟수=%d\n",
             k, phase, delay, werr, pm.MovementNeeded, pm.Rewrite, pr.MovementNeeded, pr.Rewrite, pr.DisinfectionCount,
             blk7Empty, (unsigned)buzz_count(50), rtc.HasAlarm(2), disinfectionOption.GetCount());
        CHECK(alarmSet && werr && pm.MovementNeeded && pm.Rewrite == 0,
              "⑫ 전제: 1차 시작 뒤 이동 커밋은 닿았고(MV=1·RW=0) 확인 읽기 실패로 Write Error 가 났다");
        CHECK(pr.MovementNeeded && pr.Rewrite == 0 && pr.DisinfectionCount == 1 && blk7Empty && buzz_count(50) == 1 &&
              !rtc.HasAlarm(2) && disinfectionOption.GetCount() == 0,
              "⑫(16차 A2) 실패음 뒤 1.2~2초 재접촉은 초 위상과 무관하게 이동 재확인 — 태그 불변·성공음·알람 해제·횟수 그대로");
        if (k == 0)
        {
            sim_advance_ms(3000); logs_clear(); touch(a);                   // 재확인 뒤 되넣기
            const Process p2 = get_process(a);
            tlog_p("⑫a 되넣음", a);
            CHECK(p2.Rewrite == 2 && p2.DisinfectionCount == 2 && p2.MachineNumber == 2 && disinfectionOption.GetCount() == 1,
                  "⑫a 재확인 뒤 되넣기 접촉은 2차 시작(새 액 횟수 1) — 실패 표지는 한 번만 쓰인다");
        }
    }
    // ⑫b 성공한 이동 뒤 빠른 되넣기(3초)는 2차 시작 — 재확인이 삼키지 않는다(A→A 정상 · 실패 표지 없음 · 섹터6 2초 밖) · ⑫c 0.5초 튐은 재확인
    {
        hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(9);
        started_at2(a, 0x85, 85);
        rtc_set(rel_date(13, 0, 0)); touch(clr);
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 이동(성공)
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 3초 뒤 되넣기
        const Process p = get_process(a);
        tlog_p("⑫b 되넣음", a);
        CHECK(p.Rewrite == 2 && p.DisinfectionCount == 2 && disinfectionOption.GetCount() == 1,
              "⑫b 성공한 이동 뒤 3초 되넣기는 2차 시작이다(재확인은 실패 표지가 있을 때만)");
        started_at2(a, 0x86, 86);
        rtc_set(rel_date(13, 30, 0)); touch(clr);
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 이동
        sim_advance_ms(500); logs_clear(); buzz_clear(); touch(a);          // 튐
        const Process pt = get_process(a);
        tlog_p("⑫c 튐", a);
        CHECK(pt.MovementNeeded && pt.Rewrite == 0 && pt.DisinfectionCount == 1 && buzz_count(50) == 1,
              "⑫c 이동 직후 0.5초 튐은 재확인(섹터6 시각 2초 안 · 튐 방지)");
    }
    // ⑫d 도착 기기(3): 이동 1초 뒤 대도 2차 시작(재확인은 같은 기기에서만) — 재부팅 모형이 시간을 먹으므로 rtc 를 이동 시각으로 되돌린다
    {
        hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(9);
        started_at2(a, 0x87, 87);
        rtc_set(rel_date(14, 0, 0)); touch(clr);
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 이동 @2
        const DateTime moved = rtc.GetCurrentDateTime();
        hard_reset(false, 2); deviceOption.SetNumber(3); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0);
        rtc_set(DateTime(moved.unixtime() + 1));
        logs_clear(); touch(a);
        const Process p3 = get_process(a);
        tlog_p("⑫d 도착기3", a);
        CHECK(p3.Rewrite == 2 && p3.DisinfectionCount == 2 && p3.MachineNumber == 3,
              "⑫d 도착 기기에서는 이동 1초 뒤라도 2차 시작이다(재확인은 같은 기기에서만)");
    }
    // ⑫e 일회성 담당자 ON 에서도 재확인은 담당자 관문 앞이라 거부되지 않는다
    {
        hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(9);
        recordOption.SetManagerDisposability(true);
        started_at2(a, 0x88, 88);
        rtc_set(rel_date(15, 0, 0)); touch(clr);
        sim_advance_ms(3000);
        a.readErrBlock = SECTOR1_PROCESS; a.readErrSkip = 1; a.readErrTimes = 30;
        logs_clear(); touch(a, 1, 4);                                       // 이동 커밋 뒤 확인 실패
        const bool werrE = lcd_has("Write Error");
        a.readErrBlock = -1; a.readErrTimes = 0; a.readErrSkip = 0;
        sim_advance_ms(1500); logs_clear(); buzz_clear(); touch(a);
        const Process pe = get_process(a);
        tlog("  ⑫e 일회성 ON: WriteError=%d NoManager=%d MV=%u RW=%u 성공음=%u\n", werrE, lcd_has("No Manager Info"),
             pe.MovementNeeded, pe.Rewrite, (unsigned)buzz_count(50));
        CHECK(werrE && !lcd_has("No Manager Info") && pe.MovementNeeded && pe.Rewrite == 0 && buzz_count(50) == 1,
              "⑫e 일회성 담당자 ON 이어도 실패음 뒤 재접촉은 재확인이다(담당자 관문 앞)");
        recordOption.SetManagerDisposability(false);
    }
    // ⑫f 실패 표지가 없을 때 재확인 판정의 섹터6 읽기가 실패하면 Read Error — 재확인도 2차 시작도 아니다
    {
        hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(9);
        started_at2(a, 0x89, 89);
        rtc_set(rel_date(16, 0, 0)); touch(clr);
        sim_advance_ms(3000); logs_clear(); touch(a);                       // 이동(성공)
        sim_advance_ms(500);
        a.readErrBlock = SECTOR6_DISINFECTION_END; a.readErrSkip = 0; a.readErrTimes = 30;
        logs_clear(); buzz_clear(); touch(a);
        a.readErrBlock = -1; a.readErrTimes = 0;
        const Process pf = get_process(a);
        tlog("  ⑫f 섹터6 읽기 실패: ReadError=%d MV=%u RW=%u DC=%u 성공음=%u\n", lcd_has("Read Error"), pf.MovementNeeded, pf.Rewrite,
             pf.DisinfectionCount, (unsigned)buzz_count(50));
        CHECK(lcd_has("Read Error") && pf.MovementNeeded && pf.Rewrite == 0 && pf.DisinfectionCount == 1 && buzz_count(50) == 0,
              "⑫f 섹터6 읽기 실패면 Read Error — 재확인도 2차 시작도 아니다(판정 불가)");
    }

    // ── ⑬ (16차) 1차 시작 5초 뒤 **다른 기기**에 대면 재시작이 아니라 Other Machine — 재시작은 같은 기기에서만(A2 의 형제) ──
    {
        hard_reset(false, 2); deviceOption.SetNumber(2); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(0);
        make_tag(a, 0x8A, SCOPE_TYPE_TAG, 90, "SC90", "SER");
        set_process(a, Process{1, 0, 1, 0, 1, false, 0, 1});
        set_record(a, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
        set_record(a, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
        rtc_set(rel_date(17, 0, 0)); touch(a);                              // 1차 시작 @2
        const DateTime started = rtc.GetCurrentDateTime();
        hard_reset(false, 2); deviceOption.SetNumber(3); managerOption.SetData(mk, mn);
        disinfectionOption.SetSimultaneousDisinfectionSlot(0); disinfectionOption.SetCount(0);
        rtc_set(DateTime(started.unixtime() + 5));
        logs_clear(); buzz_clear(); touch(a);
        const Process p = get_process(a);
        tlog("  ⑬ 5초 뒤 기기3: OtherMachine=%d 기기=%u 횟수3=%d\n", lcd_has("Other Machine"), p.MachineNumber, disinfectionOption.GetCount());
        CHECK(lcd_has("Other Machine") && p.MachineNumber == 2 && disinfectionOption.GetCount() == 0,
              "⑬(16차) 시작 10초 안이라도 다른 기기에선 재시작이 아니라 Other Machine — 시작 기기가 조용히 바뀌지 않는다");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
