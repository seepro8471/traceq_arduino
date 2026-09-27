// GG2 ③ — 담당자 카드 한 번으로 스코프 **여러 개를 연속** 처리할 때 담당자·일회성 표지가 건마다 옳게 갈리는가.
// 담당자를 둘 이상 두고(MGRA~MGRG) 건마다 다른 값으로 재서 "앞 건의 담당자가 뒤 건에 남는가" 를 값으로 가른다.
#include "common.h"

static SimCard mgr, s1, s2;

static void manager(uint8_t uid, const char *id, const char *name)
{
    make_tag(mgr, uid, MANAGER_TYPE_TAG, 7, id, name);
    logs_clear();
    touch(mgr);
}

static void washed(SimCard &c, uint8_t uid, int no)   // 세척 전 스코프(RW=0)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(c, Process{1, 0, 0, 0, 0, false, 0, 0});
}
static void washed_for_d(SimCard &c, uint8_t uid, int no)   // 세척까지 끝난 스코프(소독기용)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(c, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}

static bool blk_is(const SimCard &c, uint8_t b, const char *s)
{
    return memcmp(c.data[b], s, strlen(s)) == 0;
}
static int group_of(const SimCard &c)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[SECTOR14_DISINFECTION_DETAIL], sizeof(d));
    return d.GroupNumber;
}
static void tlog_mgr(const char *tag, const SimCard &c)
{
    tlog("  %s 시작담당자=[%.6s/%.6s] 종료담당자=[%.6s/%.6s] WS=%u RW=%u\n", tag,
         (const char *)c.data[SECTOR3_WASHING_START_MANAGER_KEY],
         (const char *)c.data[SECTOR3_WASHING_START_MANAGER_NAME],
         (const char *)c.data[SECTOR4_WASHING_END_MANAGER_KEY],
         (const char *)c.data[SECTOR4_WASHING_END_MANAGER_NAME],
         get_process(c).WashingStatus, get_process(c).Rewrite);
}

int main()
{
    rtc_set(rel_date(9, 0, 0));
    boot('W');
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);

    // ══ 1부 · 일회성 OFF — 담당자 한 번으로 여러 건 ══
    manager(0x01, "MGRA", "NAMEA");
    washed(s1, 0x31, 31);
    logs_clear(); touch(s1);
    tlog_mgr("31", s1);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRA") &&
          blk_is(s1, SECTOR3_WASHING_START_MANAGER_NAME, "NAMEA") && get_process(s1).WashingStatus == 1,
          "W0 양성대조: 첫 스코프가 등록된 담당자(A)로 시작 기록된다");

    sim_advance_ms(5000);
    washed(s2, 0x32, 32);
    logs_clear(); touch(s2);
    tlog_mgr("32", s2);
    CHECK(blk_is(s2, SECTOR3_WASHING_START_MANAGER_KEY, "MGRA") &&
          blk_is(s2, SECTOR3_WASHING_START_MANAGER_NAME, "NAMEA"),
          "W1 담당자 카드 한 번으로 둘째 스코프도 같은 담당자(A)");

    manager(0x02, "MGRB", "NAMEB");
    sim_advance_ms(5000);
    logs_clear(); touch(s1);                       // 31 종료 터치(2초 창 밖)
    tlog_mgr("31end", s1);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRA"),
          "W2 담당자를 바꿔도 앞 건의 **시작** 담당자는 그대로(A)");
    CHECK(blk_is(s1, SECTOR4_WASHING_END_MANAGER_KEY, "MGRB") &&
          blk_is(s1, SECTOR4_WASHING_END_MANAGER_NAME, "NAMEB"),
          "W3 종료는 **그때 등록된** 담당자(B)로 기록된다(설계 · load_manager_data)");
    CHECK(blk_is(s2, SECTOR3_WASHING_START_MANAGER_KEY, "MGRA"),
          "W4 다른 카드(32)의 담당자 블록은 건드려지지 않는다");

    sim_advance_ms(5000);
    washed(s1, 0x33, 33);
    logs_clear(); touch(s1);
    tlog_mgr("33", s1);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRB") &&
          blk_is(s1, SECTOR3_WASHING_START_MANAGER_NAME, "NAMEB"),
          "W5 담당자를 바꾼 뒤 새 건은 새 담당자(B)");

    // ══ 2부 · 일회성 ON — 한 번 충전으로 한 건만 ══
    recordOption.SetManagerDisposability(true);
    manager(0x03, "MGRC", "NAMEC");
    sim_advance_ms(5000);
    washed(s1, 0x34, 34);
    logs_clear(); touch(s1);
    tlog_mgr("34", s1);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRC") && get_process(s1).WashingStatus == 1,
          "W6 양성대조: 일회성 충전 뒤 첫 건은 담당자(C)로 시작된다");

    sim_advance_ms(5000);
    washed(s2, 0x35, 35);
    logs_clear(); touch(s2);
    tlog("  35(둘째) NoManager=%d WS=%u 시작블록비었나=%d\n", lcd_has("No Manager"),
         get_process(s2).WashingStatus, s2.data[SECTOR3_WASHING_START_MANAGER_KEY][0] == 0);
    CHECK(lcd_has("No Manager") && get_process(s2).WashingStatus == 0 &&
          s2.data[SECTOR3_WASHING_START_MANAGER_KEY][0] == 0,
          "W7 일회성 표지는 한 건에 소모된다 — 둘째 건은 거부되고 태그에 한 바이트도 안 써진다");

    manager(0x03, "MGRC", "NAMEC");                // 같은 담당자를 다시 대면 다시 충전된다
    sim_advance_ms(5000);
    logs_clear(); touch(s2);
    tlog_mgr("35재", s2);
    CHECK(blk_is(s2, SECTOR3_WASHING_START_MANAGER_KEY, "MGRC") && get_process(s2).WashingStatus == 1,
          "W8 같은 담당자를 다시 대면 다시 충전된다");

    // 거부되는 태그를 끼워 넣는다 — 일회성 표지를 소모하나
    manager(0x04, "MGRD", "NAMED");
    make_tag(mgr, 0x05, CLEAR_TYPE_TAG, 0, "CLR", "CLR");   // 세척기에서 클리어 태그 = 거부
    sim_advance_ms(5000);
    logs_clear(); touch(mgr);
    tlog("  거부 태그: InvalidTagType=%d\n", lcd_has("Invalid Tag Type"));
    CHECK(lcd_has("Invalid Tag Type"), "W9 세척기에서 클리어 태그는 거부된다");
    sim_advance_ms(5000);
    washed(s1, 0x36, 36);
    logs_clear(); touch(s1);
    tlog_mgr("36", s1);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRD") && get_process(s1).WashingStatus == 1,
          "W10 거부된 태그가 일회성 표지를 소모하지 않는다(D 로 정상 시작)");

    // 실패한 시작 뒤 — 표지를 소모하나
    manager(0x06, "MGRE", "NAMEE");
    sim_advance_ms(5000);
    washed(s2, 0x37, 37);
    s2.nackBlock = SECTOR3_WASHING_END;            // 자동 종료 쓰기만 실패(커밋 전)
    logs_clear(); touch(s2);
    tlog("  37 실패: WriteError=%d WS=%u\n", lcd_has("Write Error"), get_process(s2).WashingStatus);
    CHECK(lcd_has("Write Error") && get_process(s2).WashingStatus == 0, "W11 커밋 전 실패는 시작되지 않는다");
    s2.nackBlock = -1;
    sim_advance_ms(5000);
    logs_clear(); touch(s2);
    tlog_mgr("37재", s2);
    CHECK(blk_is(s2, SECTOR3_WASHING_START_MANAGER_KEY, "MGRE") && get_process(s2).WashingStatus == 1,
          "W12 실패한 시작은 일회성 표지를 소모하지 않는다(E 로 재접촉 성공)");

    // 담당자를 연달아 바꾸며 건마다 처리
    manager(0x07, "MGRF", "NAMEF");
    sim_advance_ms(5000);
    washed(s1, 0x38, 38);
    logs_clear(); touch(s1);
    manager(0x08, "MGRG", "NAMEG");
    sim_advance_ms(5000);
    washed(s2, 0x39, 39);
    logs_clear(); touch(s2);
    tlog_mgr("38", s1); tlog_mgr("39", s2);
    CHECK(blk_is(s1, SECTOR3_WASHING_START_MANAGER_KEY, "MGRF") &&
          blk_is(s2, SECTOR3_WASHING_START_MANAGER_KEY, "MGRG"),
          "W13 담당자를 연달아 바꾸면 건마다 그 담당자로 갈린다(F·G)");

    // ══ 3부 · 소독기 연속 배치(동시소독 slot=2) ══
    deviceOption.SetType('D');
    hard_reset(false, 2);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
    disinfectionOption.SetSimultaneousDisinfectionSlot(2);
    disinfectionOption.SetSimultaneousDisinfectionDelay(5);
    disinfectionOption.SetMaximumCount(0);
    disinfectionOption.SetCount(0);
    manager(0x09, "MGRH", "NAMEH");

    int g[4]{};
    int cnt0 = disinfectionOption.GetCount();
    for (uint8_t i = 0; i < 4; ++i)
    {
        SimCard &c = (i & 1) ? s2 : s1;
        washed_for_d(c, (uint8_t)(0x61 + i), 61 + i);
        sim_advance_ms(60000);
        logs_clear(); touch(c);
        g[i] = group_of(c);
        tlog("  배치 %u: 스코프 %d 그룹=%d 횟수=%d 담당자=[%.6s]\n", i, 61 + i, g[i],
             disinfectionOption.GetCount(), (const char *)c.data[SECTOR5_DISINFECTION_START_MANAGER_KEY]);
        if (i == 1)
            CHECK(blk_is(c, SECTOR5_DISINFECTION_START_MANAGER_KEY, "MGRH"),
                  "D0 연속 배치의 둘째 스코프도 같은 담당자(H)");
    }
    CHECK(g[0] == 1 && g[1] == 2 && g[2] == 1 && g[3] == 2,
          "D1 연속 4건의 그룹이 host·guest·host·guest 로 갈린다");
    CHECK(disinfectionOption.GetCount() == cnt0 + 2,
          "D2 연속 4건(동시소독 2배치)의 소독 횟수는 +2");

    // ══ 4부 · ④ 누적 드리프트 — 동시소독 끄고 20건 연속 ══
    disinfectionOption.SetSimultaneousDisinfectionSlot(0);
    disinfectionOption.SetCount(0);
    uint8_t first[9]{}, last[9]{};
    int firstGroup = 0, lastGroup = 0;
    uint16_t firstOps = 0, lastOps = 0, maxOps = 0, minOps = 0xFFFF;
    bool everyStep = true;
    for (uint8_t i = 0; i < 20; ++i)
    {
        SimCard &c = (i & 1) ? s2 : s1;
        washed_for_d(c, (uint8_t)(0x80 + i), 80 + i);
        c.opCount = 0;                      // 건당 카드 동작 수(t_ops 예산) 가 건마다 같은가
        sim_advance_ms(60000);
        logs_clear(); touch(c);
        if (disinfectionOption.GetCount() != i + 1) everyStep = false;
        if (c.opCount > maxOps) maxOps = c.opCount;
        if (c.opCount < minOps) minOps = c.opCount;
        if (i == 0)  { memcpy(first, c.data[SECTOR1_PROCESS], 9); firstGroup = group_of(c); firstOps = c.opCount; }
        if (i == 19) { memcpy(last,  c.data[SECTOR1_PROCESS], 9); lastGroup  = group_of(c); lastOps  = c.opCount; }
    }
    tlog("  20건 카드동작: 첫=%u 끝=%u 최소=%u 최대=%u\n", firstOps, lastOps, minOps, maxOps);
    CHECK(firstOps == lastOps && minOps == maxOps && firstOps > 0,
          "D6 건당 카드 동작 수가 20건 내내 같다(접촉 예산 드리프트 0)");
    // Process 의 MachineNumber(4~5바이트)는 기기번호라 건마다 같다 — 9바이트 전부 같아야 한다
    tlog("  20건: 횟수=%d 매건+1=%d 첫그룹=%d 끝그룹=%d Process같음=%d 끝담당자=[%.6s]\n",
         disinfectionOption.GetCount(), everyStep, firstGroup, lastGroup,
         memcmp(first, last, 9) == 0, (const char *)s2.data[SECTOR5_DISINFECTION_START_MANAGER_KEY]);
    CHECK(everyStep && disinfectionOption.GetCount() == 20, "D3 20건 연속 — 소독 횟수가 매 건 정확히 +1");
    CHECK(memcmp(first, last, 9) == 0 && firstGroup == 1 && lastGroup == 1,
          "D4 20건째 태그의 Process 9바이트·그룹이 **첫 건과 같다**(누적 드리프트 0)");
    CHECK(blk_is(s2, SECTOR5_DISINFECTION_START_MANAGER_KEY, "MGRH"),
          "D5 20건째도 담당자(H)가 그대로 기록된다");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
