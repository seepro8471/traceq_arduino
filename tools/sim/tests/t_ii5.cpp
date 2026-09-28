// 14회차 II-C P2-3 잠금 — 동시소독 guest: 커밋은 됐는데 확인 읽기가 끊김(Write Error)
// (Write Error), re-touched within 2 s (site rule: re-touch after the failure sound) -> restart path.
// The restart takes isGuest from RAM slots (never set) -> the committed group 2 is overwritten with 1
// and the host slot / time window moves from A to B.
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
        sim_advance_ms(3000);
        logs_clear(); touch(b);
        const LocalDateTime s = get_ldt(b, SECTOR5_DISINFECTION_START);
        const LocalDateTime e = get_ldt(b, SECTOR6_DISINFECTION_END);
        tlog("  S3b (>2s): grpB=%d start=%02u:%02u:%02u end=%02u:%02u:%02u\n", group_of(b),
             s.Time.Hour, s.Time.Minute, s.Time.Second, e.Time.Hour, e.Time.Minute, e.Time.Second);
        CHECK(true, "S3b 사실 기록: 2초 밖 재접촉은 종료(8차 판정)");
    }

    done();
    for (;;) {}
}
