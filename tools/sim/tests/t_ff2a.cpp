// FF2-a — 여러 기기가 같은 스코프를 주고받는 **하루 전체**.
//   세척기 W#1 → 소독기 D#2 → (액교환·이동) 소독기 D#3 → 서버 S#9.
//   이 장치엔 EEPROM 이 하나뿐이라 "다른 기기" 는 **그 기기의 EEPROM 178바이트를 되돌리고 RAM 을 새로
//   만드는 것**(hard_reset)으로 모델한다 — 기기마다 담당자·소독 횟수·교환일·MaxCount 가 다르게 둔다.
// ★표본 규율: 태그는 **어제 흔적이 남은 것**으로만 쟀다(어제 세척·소독 기록 · 어제 담당자 ·
//   어제 이동 주기의 섹터7·8·DETAIL2 · 덤프까지 끝나 Process 만 0).
#include "common.h"

static SimCard sc, mgr, clr;

// ── 기기 모델: 옵션 EEPROM 0..177 ──
static const int kEeLen = 178;
struct DevEe { uint8_t b[kEeLen]; };
static DevEe devs[4];
static void dev_save(uint8_t i) { for (int a = 0; a < kEeLen; ++a) devs[i].b[a] = EEPROM.read(a); }
static void dev_switch(uint8_t i)
{
    card_remove();
    for (int a = 0; a < kEeLen; ++a) EEPROM.update(a, devs[i].b[a]);
    hard_reset(false, 2);   // 그 기기의 RAM(host/guest·이동 플래그·알람·일회성)은 비어 있다
}

static void set_mgr(const char *key, const char *name)
{
    unsigned char k[ManagerOption::KEY_SIZE]{}, n[ManagerOption::NAME_SIZE]{};
    strncpy((char *)k, key, sizeof(k));
    strncpy((char *)n, name, sizeof(n));
    managerOption.SetData(k, n);
}

static int dev_of(const SimCard &c, uint8_t block)   // 레코드 머리 2바이트 = 기기번호
{
    int d = 0;
    memcpy(&d, c.data[block], 2);
    return d;
}
static int group_of(const SimCard &c, uint8_t block)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[block], sizeof(d));
    return d.GroupNumber;
}
static LocalDateTime lcd_of(const SimCard &c, uint8_t block)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[block], sizeof(d));
    return d.DateTime;
}
static const char *mgr_of(const SimCard &c, uint8_t block) { return (const char *)c.data[block]; }

// 어제 흔적이 남은 스코프(어제 덤프까지 끝나 Process 만 0 · 기록 블록은 그대로)
static void yesterday_scope(SimCard &c, uint8_t uid, int no)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, no, "SC0042", "S0042");
    set_process(c, Process{0, 0, 0, 0, 0, false, 0, 0});
    const DateTime y = rel_date(8, 0, 0) - TimeSpan{86400L};
    set_record(c, SECTOR2_WASHING_START, 7, y);
    set_record(c, SECTOR3_WASHING_END,   7, y + TimeSpan{240L});
    set_record(c, SECTOR5_DISINFECTION_START, 8, y + TimeSpan{300L});
    set_record(c, SECTOR6_DISINFECTION_END,   8, y + TimeSpan{1380L});
    set_record(c, SECTOR7_DISINFECTION_START, 9, y + TimeSpan{1500L});   // 어제는 이동 주기였다
    set_record(c, SECTOR8_DISINFECTION_END,   9, y + TimeSpan{2580L});
    const uint8_t mgrBlocks[] = {SECTOR3_WASHING_START_MANAGER_KEY, SECTOR3_WASHING_START_MANAGER_NAME,
                                 SECTOR4_WASHING_END_MANAGER_KEY,  SECTOR4_WASHING_END_MANAGER_NAME,
                                 SECTOR5_DISINFECTION_START_MANAGER_KEY, SECTOR5_DISINFECTION_START_MANAGER_NAME,
                                 SECTOR6_DISINFECTION_END_MANAGER_KEY,   SECTOR6_DISINFECTION_END_MANAGER_NAME,
                                 SECTOR7_DISINFECTION_START_MANAGER_KEY, SECTOR7_DISINFECTION_START_MANAGER_NAME,
                                 SECTOR8_DISINFECTION_END_MANAGER_KEY,   SECTOR8_DISINFECTION_END_MANAGER_NAME};
    for (uint8_t i = 0; i < sizeof(mgrBlocks); ++i) put_block(c, mgrBlocks[i], "OLDMGR", 6);
    DisinfectionDetail d{};
    d.GroupNumber = 2;                                      // 어제는 guest 였다
    d.DateTime = DefaultRtc::ToLocalDateTime(y - TimeSpan{86400L * 3});
    put_block(c, SECTOR14_DISINFECTION_DETAIL, &d, sizeof(d));
    d.GroupNumber = 1;
    put_block(c, SECTOR14_DISINFECTION_DETAIL2, &d, sizeof(d));
    put_block(c, SECTOR2_PATIENT_KEY, "OLDPID", 6);          // 어제 환자(게이트웨이 없는 현장)
    put_block(c, SECTOR2_PATIENT_NAME, "OLDNAME", 7);
}

static void server_auth()
{
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}

int main()
{
    rtc_set(rel_date(8, 0, 0));
    boot('W');

    // ── 기기 넷 만들기(각자 자기 EEPROM) ──
    deviceOption.SetNumber(1); deviceOption.SetType('W');
    alarmOption.SetTimeSlot1(4); alarmOption.SetTimeSlot2(18);
    set_mgr("W1KEY", "WASHER1"); dev_save(0);

    deviceOption.SetNumber(2); deviceOption.SetType('D');
    disinfectionOption.SetCount(25); disinfectionOption.SetMaximumCount(30);
    disinfectionOption.SetClearDateTime(DefaultRtc::ToLocalDateTime(rel_date(8, 0, 0) - TimeSpan{86400L * 5}));
    disinfectionOption.SetClearPending(DisinfectionOption::kPendingNone);
    set_mgr("D2KEY", "DISIN2"); dev_save(1);

    deviceOption.SetNumber(3); deviceOption.SetType('D');
    disinfectionOption.SetCount(4); disinfectionOption.SetMaximumCount(30);
    disinfectionOption.SetClearDateTime(DefaultRtc::ToLocalDateTime(rel_date(8, 0, 0) - TimeSpan{86400L * 2}));
    set_mgr("D3KEY", "DISIN3"); dev_save(2);

    deviceOption.SetNumber(9); deviceOption.SetType('S');
    set_mgr("S9KEY", "SERVER9"); dev_save(3);

    make_tag(mgr, 0x11, MANAGER_TYPE_TAG, 0, "MGRID", "MGRSER");
    make_tag(clr, 0x12, CLEAR_TYPE_TAG, 0, "CLR", "CLR");

    // ══ 하루 A — 이동 없는 정상 하루 (W#1 → D#2 → S#9) ══
    yesterday_scope(sc, 0x42, 42);
    {
        rtc_set(rel_date(9, 0, 0));
        dev_switch(0);
        touch(sc);                                    // 세척 시작
        const int w1 = dev_of(sc, SECTOR2_WASHING_START);
        rtc_set(rel_date(9, 20, 0));
        touch(sc);                                    // 세척 종료
        const int w2 = dev_of(sc, SECTOR3_WASHING_END);
        Process p = get_process(sc);
        tlog("  A 세척: 시작기기=%d 종료기기=%d WS=%u RW=%u MV=%u DC=%u 담당자=[%.7s]\n",
             w1, w2, p.WashingStatus, p.Rewrite, (unsigned)p.MovementNeeded, p.DisinfectionCount,
             mgr_of(sc, SECTOR3_WASHING_START_MANAGER_KEY));
        CHECK(w1 == 1 && w2 == 1, "A1 세척 기록 둘 다 세척기 번호 1");
        CHECK(p.WashingStatus == 1 && p.Rewrite == 1 && p.DisinfectionCount == 0 && !p.MovementNeeded,
              "A2 세척 시작이 어제 계수기(DC=1·MV·RW)를 전부 0 으로 되돌린다");
        CHECK(strncmp(mgr_of(sc, SECTOR3_WASHING_START_MANAGER_KEY), "W1KEY", 5) == 0,
              "A3 세척 담당자 = 세척기 자기 EEPROM 담당자(어제 OLDMGR 가 남지 않는다)");

        rtc_set(rel_date(9, 25, 0));
        dev_switch(1);                                 // 소독기 D#2
        const int c0 = disinfectionOption.GetCount();
        touch(sc);                                     // 소독 시작
        const int d1 = dev_of(sc, SECTOR5_DISINFECTION_START);
        p = get_process(sc);
        const LocalDateTime lcdA = lcd_of(sc, SECTOR14_DISINFECTION_DETAIL);
        rtc_set(rel_date(9, 45, 0));
        touch(sc);                                     // 소독 종료
        const int d2 = dev_of(sc, SECTOR6_DISINFECTION_END);
        tlog("  A 소독: 시작기기=%d 종료기기=%d MN=%d DC=%u RW=%u 그룹=%d 횟수 %d->%d\n",
             d1, d2, p.MachineNumber, p.DisinfectionCount, p.Rewrite,
             group_of(sc, SECTOR14_DISINFECTION_DETAIL), c0, disinfectionOption.GetCount());
        tlog_ldt("A DETAIL 액교환일(D#2 것이어야)", lcdA);
        CHECK(d1 == 2 && d2 == 2 && p.MachineNumber == 2, "A4 소독 기록·Process 기기번호 = 소독기 2");
        CHECK(group_of(sc, SECTOR14_DISINFECTION_DETAIL) == 1,
              "A5 단독 소독은 그룹1 — 어제의 그룹2 가 남지 않는다");
        CHECK(lcdA.Date.Day == (uint8_t)(rel_date(8, 0, 0) - TimeSpan{86400L * 5}).day(),
              "A6 DETAIL 액교환일 = 그 소독기(D#2) 의 교환일");
        CHECK(disinfectionOption.GetCount() == c0 + 1, "A7 소독 횟수는 그 소독기에서만 +1");
        CHECK(strncmp(mgr_of(sc, SECTOR5_DISINFECTION_START_MANAGER_KEY), "D2KEY", 5) == 0,
              "A8 소독 담당자 = 소독기 자기 담당자(세척기 담당자가 섞이지 않는다)");
        // 세척 기록은 소독기가 건드리지 않는다
        CHECK(dev_of(sc, SECTOR2_WASHING_START) == 1 && dev_of(sc, SECTOR3_WASHING_END) == 1,
              "A9 소독 경로가 세척 기록(기기 1)을 덮지 않는다");

        dev_switch(3);                                 // 서버 S#9
        server_auth();
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        tlog("  A 덤프: 1714(0200)=%d 1B18(0200)=%d 1F1C=%d 2320=%d 3B38=%d Ok=%d\n",
             serial_has("17140200"), serial_has("1B180200"), serial_has("1F1C"), serial_has("2320"),
             serial_has("3B38"), serial_has("Ok!"));
        CHECK(serial_has("17140200") && serial_has("1B180200"),
              "A10 덤프 줄에 소독 시작·종료가 소독기 2 로 나간다");
        CHECK(!serial_has("1F1C") && !serial_has("2320"),
              "A11 이동 없는 하루의 덤프에는 섹터7·8 줄이 없다(어제 이동 기록이 되살아나지 않는다)");
        CHECK(serial_has("Ok!"), "A12 덤프 Ok!");
        p = get_process(sc);
        CHECK(p.WashingStatus == 0 && p.DisinfectionCount == 0 && p.Rewrite == 0,
              "A13 덤프 뒤 Process 0");
    }

    // ══ 하루 B — 액교환·이동이 낀 하루 (W#1 → D#2 → 클리어@D#2 → 이동 → D#3 2차 → S#9) ══
    {
        rtc_set(rel_date(13, 0, 0));
        dev_switch(0);
        touch(sc);                                     // 세척 시작
        rtc_set(rel_date(13, 20, 0));
        touch(sc);                                     // 세척 종료

        rtc_set(rel_date(13, 25, 0));
        dev_switch(1);                                 // D#2
        const int c2a = disinfectionOption.GetCount();
        touch(sc);                                     // 1차 소독 시작
        // 액교환 — 클리어 태그는 **그 소독기**에 이동 플래그를 세운다
        rtc_set(rel_date(13, 40, 0));
        const int clrCnt = disinfectionOption.GetClearCount();
        touch(clr);
        const int c2b = disinfectionOption.GetCount();
        tlog("  B 클리어@D#2: 횟수 %d->%d 클리어횟수 %d->%d\n", c2a + 1, c2b, clrCnt,
             disinfectionOption.GetClearCount());
        CHECK(c2b == 0 && disinfectionOption.GetClearCount() == clrCnt + 1,
              "B1 클리어 태그는 그 소독기 횟수를 0 · 클리어 횟수 +1");

        rtc_set(rel_date(13, 42, 0));
        touch(sc);                                     // 이동 처리(1차 종료 + MV=1)
        Process p = get_process(sc);
        const int e1 = dev_of(sc, SECTOR6_DISINFECTION_END);
        tlog("  B 이동@D#2: MV=%u RW=%u DC=%u 1차종료기기=%d\n",
             (unsigned)p.MovementNeeded, p.Rewrite, p.DisinfectionCount, e1);
        CHECK(p.MovementNeeded && p.Rewrite == 0, "B2 이동 커밋(MV=1·RW=0)");
        CHECK(e1 == 2, "B3 이동이 쓰는 1차 종료 기록은 **출발 소독기(2)** 번호다");

        rtc_set(rel_date(13, 50, 0));
        dev_switch(2);                                 // D#3 (도착)
        const int c3a = disinfectionOption.GetCount();
        touch(sc);                                     // 2차 소독 시작
        p = get_process(sc);
        const int s2 = dev_of(sc, SECTOR7_DISINFECTION_START);
        const LocalDateTime lcdB = lcd_of(sc, SECTOR14_DISINFECTION_DETAIL2);
        rtc_set(rel_date(14, 10, 0));
        touch(sc);                                     // 2차 종료
        const int e2 = dev_of(sc, SECTOR8_DISINFECTION_END);
        tlog("  B 2차@D#3: 시작기기=%d 종료기기=%d MN=%d DC=%u 횟수 %d->%d\n",
             s2, e2, p.MachineNumber, p.DisinfectionCount, c3a, disinfectionOption.GetCount());
        tlog_ldt("B DETAIL2 액교환일(D#3 것이어야)", lcdB);
        // ★기기번호만 보면 2차 종료가 **거부돼도 통과**한다 — `disinfection_start` 가 섹터8 에 도착기 번호로
        //  자동 종료를 **미리** 쓰기 때문이다(GG1 P3-2). 그래서 **시각**으로 갈라야 한다(미리채움 ≠ 14:10).
        // ★`lcd_of`(DETAIL 레이아웃)로 읽으면 26:00 같은 값이 나온다 — 종료 블록은 **레코드**(번호+일시)다.
        const LocalDateTime e2t = get_ldt(sc, SECTOR8_DISINFECTION_END);
        tlog("  B 2차 종료시각=%02u:%02u (미리채움이 아니라 접촉 시각 14:10 이어야)\n",
             e2t.Time.Hour, e2t.Time.Minute);
        CHECK(s2 == 3 && e2 == 3, "B4 2차 기록은 도착 소독기(3) 번호");
        CHECK(e2t.Time.Hour == 14 && e2t.Time.Minute == 10,
              "B4b 2차 종료 시각이 **접촉 시각**이다(미리채움이 아니다 — 거부됐으면 미리채움이 남는다)");
        CHECK(p.DisinfectionCount == 2 && p.MachineNumber == 3, "B5 DC=2 · Process 기기번호는 도착기(3)");
        CHECK(disinfectionOption.GetCount() == c3a + 1, "B6 2차 소독도 그 기기 횟수를 +1");
        CHECK(dev_of(sc, SECTOR5_DISINFECTION_START) == 2 && dev_of(sc, SECTOR6_DISINFECTION_END) == 2,
              "B7 2차 소독이 1차 기록(출발기 2)을 덮지 않는다");
        CHECK(lcdB.Date.Day == (uint8_t)(rel_date(8, 0, 0) - TimeSpan{86400L * 2}).day(),
              "B8 DETAIL2 액교환일 = 도착 소독기(D#3) 의 교환일");
        CHECK(strncmp(mgr_of(sc, SECTOR7_DISINFECTION_START_MANAGER_KEY), "D3KEY", 5) == 0 &&
              strncmp(mgr_of(sc, SECTOR6_DISINFECTION_END_MANAGER_KEY), "D2KEY", 5) == 0,
              "B9 1차 종료 담당자=D#2 · 2차 시작 담당자=D#3 (기기별 담당자가 섞이지 않는다)");

        dev_switch(3);
        server_auth();
        logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        tlog("  B 덤프: 1714(0200)=%d 1B18(0200)=%d 1F1C(0300)=%d 2320(0300)=%d 3B38=%d 3B39=%d Ok=%d\n",
             serial_has("17140200"), serial_has("1B180200"), serial_has("1F1C0300"),
             serial_has("23200300"), serial_has("3B38"), serial_has("3B39"), serial_has("Ok!"));
        CHECK(serial_has("17140200") && serial_has("1B180200") &&
              serial_has("1F1C0300") && serial_has("23200300"),
              "B10 이동 하루의 덤프 줄 집합 = 1차(기기2) + 2차(기기3) 넷 전부");
        CHECK(serial_has("Ok!"), "B11 이동 하루 덤프 Ok!");
    }

    done();
    for (;;) {}
}
