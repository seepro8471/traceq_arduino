// 14회차 II-C P2-3 잠금 — 동시소독 guest 의 커밋은 됐는데 확인 읽기가 끊겨 Write Error 가 난 뒤 사람이 다시 댄다.
//  옛 결함: 재시작이 RAM 슬롯(이 스코프를 모른다)만 보고 커밋된 그룹2 를 1 로 덮어 host·창이 A 에서 B 로 옮겨갔다.
//  S3  2초 안 재접촉(재시작) — 태그 DETAIL 의 그룹2 를 지키고, 그 뒤 C 는 대조와 같이 새 host.
//  S3b 10초 밖 재접촉 — 종료로 기록된다(대조 · 8차 판정 · 창은 15차 A3 로 10초) · S3c 3초 재접촉 = 재시작.
//  S4  S3 을 이동해 온 스코프(2차 · DETAIL2)로 — 재시작이 DETAIL2 를 읽어야 한다(15차 III-H d1).
#include "common.h"

static SimCard a, b, c;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static int group_of(const SimCard &t)
{
    DisinfectionDetail d{};
    memcpy(&d, t.data[SECTOR14_DISINFECTION_DETAIL], sizeof(d));
    return d.GroupNumber;
}
static void washed(SimCard &t, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 1});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}
static int group2_of(const SimCard &t)
{
    DisinfectionDetail d{};
    memcpy(&d, t.data[SECTOR14_DISINFECTION_DETAIL2], sizeof(d));
    return d.GroupNumber;
}
// 기기1 에서 1차를 마치고 **이동 처리된**(MV=1 · RW=0) 스코프 — 다음 접촉이 2차 시작
static void moved(SimCard &t, uint8_t uid, int no)
{
    washed(t, uid, no);
    set_process(t, Process{1, 1, 1, 1, 1, true, 0, 0});
    set_record(t, SECTOR5_DISINFECTION_START, 1, rel_date(9, 10, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 1, rel_date(9, 30, 0));
    DisinfectionDetail d{};
    d.GroupNumber = 1;
    put_block(t, SECTOR14_DISINFECTION_DETAIL, &d, sizeof(d));
}
static void as_disinfector()
{
    deviceOption.SetType('D');
    hard_reset(false, 2);
    deviceOption.SetNumber(2);
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    recordOption.SetPatientCheck(false);
    disinfectionOption.SetSimultaneousDisinfectionSlot(2);
    disinfectionOption.SetSimultaneousDisinfectionDelay(3);
    disinfectionOption.SetMaximumCount(0);
    disinfectionOption.SetCount(0);
}
static void secs(const char *tag, const SimCard &t)
{
    const LocalDateTime s = get_ldt(t, SECTOR5_DISINFECTION_START);
    tlog("  %s start=%02u:%02u:%02u grp=%d RW=%u\n", tag, s.Time.Hour, s.Time.Minute, s.Time.Second,
         group_of(t), get_process(t).Rewrite);
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');

    // ---- control: no failure ----
    int ctlCountC = -1, ctlGrpC = -1, ctlGrpB = -1;
    {
        as_disinfector();
        rtc_set(rel_date(10, 0, 0));
        washed(a, 0x21, 21); touch(a);                       // A host 10:00:00
        rtc_set(rel_date(10, 2, 30));
        washed(b, 0x22, 22); touch(b);                       // B guest (window 10:00..10:03)
        const int c0 = disinfectionOption.GetCount();
        rtc_set(rel_date(10, 4, 0));
        washed(c, 0x23, 23); touch(c);                       // C outside A's window -> new host
        ctlGrpB = group_of(b); ctlGrpC = group_of(c); ctlCountC = disinfectionOption.GetCount() - c0;
        tlog("  CTL: grpA=%d grpB=%d grpC=%d count(C)+%d total=%d\n", group_of(a), ctlGrpB, ctlGrpC, ctlCountC,
             disinfectionOption.GetCount());
        CHECK(ctlGrpB == 2 && ctlGrpC == 1 && ctlCountC == 1, "대조: B 는 guest(그룹2) · C 는 새 host(그룹1) · 횟수 +1");
    }

    // ---- S3: B commit lands, confirm fails -> Write Error -> re-touch within 2 s ----
    {
        as_disinfector();
        rtc_set(rel_date(10, 0, 0));
        washed(a, 0x31, 31); touch(a);                       // A host
        rtc_set(rel_date(10, 2, 30));
        washed(b, 0x32, 32);
        b.readErrBlock = SECTOR1_PROCESS; b.readErrSkip = 1; b.readErrTimes = 30;
        logs_clear(); touch(b, 1, 4);
        b.readErrBlock = -1; b.readErrTimes = 0; b.readErrSkip = 0;
        const int grpFirst = group_of(b);
        secs("S3 B torn", b);
        tlog("  S3 B torn: WriteError=%d fail100=%u count=%d\n", lcd_has("Write Error"), buzz_count(100),
             disinfectionOption.GetCount());
        CHECK(lcd_has("Write Error") && get_process(b).Rewrite == 2 && grpFirst == 2,
              "S3 전제: B 는 그룹2 로 커밋됐는데 기기는 Write Error 를 냈다");
        const uint32_t g0 = g_ms;
        logs_clear(); touch(b, 1, 4);                       // re-touch right after the failure sound
        tlog("  S3 re-touch after %lu ms: ok50=%u\n", (unsigned long)(g_ms - g0), buzz_count(50));
        secs("S3 B restart", b);
        const int grpAfter = group_of(b);
        CHECK(grpAfter == 2, "S3 2초 안 재접촉이 커밋된 그룹2 를 지킨다(RAM 이 모르면 태그의 DETAIL 을 믿는다)");
        const int c0 = disinfectionOption.GetCount();
        rtc_set(rel_date(10, 4, 0));
        washed(c, 0x33, 33); touch(c);                       // outside A's window, inside B's new one
        tlog("  S3: grpA=%d grpB=%d grpC=%d count(C)+%d total=%d\n", group_of(a), grpAfter, group_of(c),
             disinfectionOption.GetCount() - c0, disinfectionOption.GetCount());
        CHECK(group_of(c) == ctlGrpC && disinfectionOption.GetCount() - c0 == ctlCountC,
              "S3 그 뒤 C 는 대조와 같이 새 host(그룹1)·횟수 +1 — host·창이 B 로 옮겨지지 않는다");
    }

    // ---- S3b: same, but the re-touch is > 2 s (end path) - for comparison ----
    {
        as_disinfector();
        rtc_set(rel_date(12, 0, 0));
        washed(a, 0x41, 41); touch(a);
        rtc_set(rel_date(12, 2, 30));
        washed(b, 0x42, 42);
        b.readErrBlock = SECTOR1_PROCESS; b.readErrSkip = 1; b.readErrTimes = 30;
        touch(b, 1, 4);
        b.readErrBlock = -1; b.readErrTimes = 0; b.readErrSkip = 0;
        // 15차 사장님 A3: 재시작 창 2초 → 10초(실패음 0.8초가 창을 먼저 먹어 사람 반응이 창 밖으로 밀렸다). 3초 재접촉 = 재시작.
        sim_advance_ms(3000);
        logs_clear(); touch(b);
        const LocalDateTime s3 = get_ldt(b, SECTOR5_DISINFECTION_START);
        const LocalDateTime e3 = get_ldt(b, SECTOR6_DISINFECTION_END);
        tlog("  S3c (3s): grpB=%d RW=%u start=%02u:%02u:%02u end=%02u:%02u:%02u\n", group_of(b), get_process(b).Rewrite,
             s3.Time.Hour, s3.Time.Minute, s3.Time.Second, e3.Time.Hour, e3.Time.Minute, e3.Time.Second);
        CHECK(group_of(b) == 2 && get_process(b).Rewrite == 2 && !(e3.Time.Hour == 12 && e3.Time.Minute == 2),
              "S3c(15차 A3) 실패음 뒤 3초 재접촉은 재시작이다 — 종료 시각은 미리채움 그대로(2초짜리 소독이 되지 않는다)");
        // 10초 밖 재접촉 = 종료 — 종료로 기록됐다는 증거는 블록 종료 시각이 미리채움이 아니라 재접촉 시각(12:02)
        sim_advance_ms(12000);
        logs_clear(); touch(b);
        const LocalDateTime s = get_ldt(b, SECTOR5_DISINFECTION_START);
        const LocalDateTime e = get_ldt(b, SECTOR6_DISINFECTION_END);
        tlog("  S3b (>10s): grpB=%d start=%02u:%02u:%02u end=%02u:%02u:%02u\n", group_of(b),
             s.Time.Hour, s.Time.Minute, s.Time.Second, e.Time.Hour, e.Time.Minute, e.Time.Second);
        CHECK(group_of(b) == 2 && e.Time.Hour == 12 && e.Time.Minute == 2 && e.Time.Second >= 40,
              "S3b 사실: 10초 밖 재접촉은 종료로 기록된다(종료 시각 = 재접촉 시각 · 그룹2 그대로 · 8차 판정)");
    }

    // ---- S4: 같은 것을 **이동해 온 스코프(2차)** 로 — 재시작이 DETAIL2 를 읽어야 한다 ----
    //  15차 III-H d1: DETAIL(1차 · 그룹1)을 읽게 되돌려도 초록이었다(S3 는 1차만) → 그러면 2차 그룹2 가 1 로 덮이고 host 가 옮겨간다.
    {
        as_disinfector();
        rtc_set(rel_date(13, 0, 0));
        moved(a, 0x51, 51); touch(a);                       // A 2차 host 13:00
        rtc_set(rel_date(13, 2, 30));
        moved(b, 0x52, 52);
        b.readErrBlock = SECTOR1_PROCESS; b.readErrSkip = 1; b.readErrTimes = 30;
        touch(b, 1, 4);                                     // 2차 guest 커밋은 됐는데 확인 읽기 실패 → Write Error
        b.readErrBlock = -1; b.readErrTimes = 0; b.readErrSkip = 0;
        const int grpBefore = group2_of(b);
        logs_clear(); touch(b);                             // 2초 안 재접촉 = 재시작(RAM 은 b 를 모른다)
        const int grpAfter = group2_of(b);
        rtc_set(rel_date(13, 4, 0));
        const int c0 = disinfectionOption.GetCount();
        moved(c, 0x53, 53); touch(c);                       // 창 밖 → 새 host
        tlog("  S4 이동 스코프: grpB %d→%d grpA=%d grpC=%d count(C)+%d\n", grpBefore, grpAfter, group2_of(a), group2_of(c),
             disinfectionOption.GetCount() - c0);
        CHECK(grpBefore == 2 && grpAfter == 2,
              "S4 이동해 온 스코프의 2초 안 재접촉도 커밋된 2차 그룹2(DETAIL2)를 지킨다(1차 DETAIL 을 읽으면 1 로 덮인다)");
        CHECK(group2_of(a) == 1 && group2_of(c) == 1 && disinfectionOption.GetCount() - c0 == 1,
              "S4 그 뒤 C 는 새 host(그룹1)·횟수 +1 — host·창이 B 로 옮겨지지 않는다");
    }

    done();
    for (;;) {}
}
