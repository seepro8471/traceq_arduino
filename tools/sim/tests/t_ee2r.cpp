// 10차 — 사장님 판정(09-27)을 받치는 **회복 경로**를 잠근다.
//  사장님: "동시소독은 많이 쓴다. 다만 실패음이 나면 그 스코프를 다시 대니까 걱정 안 해도 된다."
//  → 그 "다시 댄다" 가 실제로 host 슬롯을 되찾아 **둘째 스코프가 guest(그룹2)로** 기록되는지가 판정의 근거다.
//  상태는 직접 만든다(절단점 주입에 기대지 않는다): 태그엔 소독 시작이 커밋돼 있고 리더 RAM 엔 host 가 없다
//  = 확인 읽기 직전에 카드를 뗀 뒤의 모습.
#include "common.h"

static SimCard mgr, a, b;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static int group_of(const SimCard &c)
{
    DisinfectionDetail d{};
    memcpy(&d, c.data[SECTOR14_DISINFECTION_DETAIL], sizeof(d));
    return d.GroupNumber;
}

// 세척까지 끝난 스코프
static void washed(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(c, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
}

// 소독 시작이 **커밋된** 모습(확인 읽기 직전 이탈 뒤) — 리더 RAM 의 host 는 비어 있다
static void start_committed(SimCard &c, uint8_t uid, int no, const DateTime &when)
{
    washed(c, uid, no);
    set_process(c, Process{1, 1, 1, 1, 2, false, 0, 2});
    set_record(c, SECTOR5_DISINFECTION_START, 2, when);
    DisinfectionDetail d{};
    d.GroupNumber = 1;
    put_block(c, SECTOR14_DISINFECTION_DETAIL, &d, sizeof(d));
}

static void as_disinfector()
{
    deviceOption.SetType('D');
    hard_reset(false, 2);                     // ★RAM 의 host·시간창이 비어 있는 상태로
    managerOption.SetData(mk, mn);
    disinfectionOption.SetSimultaneousDisinfectionSlot(2);
    disinfectionOption.SetSimultaneousDisinfectionDelay(5);
}

int main()
{
    rtc_set(rel_date(11, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── ① 양성대조: 온전한 흐름 — 딜레이 안 둘째 스코프는 guest(그룹2)·횟수 안 오름 ──
    bool ctlGuest = false, ctlCount = false;
    {
        as_disinfector();
        touch(mgr);
        washed(a, 0x23, 23);
        touch(a);                              // host
        const uint8_t c0 = disinfectionOption.GetCount();
        sim_advance_ms(60UL * 1000);
        washed(b, 0x24, 24);
        touch(b);                              // 1분 뒤 → guest
        ctlGuest = (group_of(a) == 1 && group_of(b) == 2);
        ctlCount = (disinfectionOption.GetCount() == c0);
        tlog("  ① 온전: a그룹=%d b그룹=%d 횟수 %u->%u\n", group_of(a), group_of(b),
             (unsigned)c0, (unsigned)disinfectionOption.GetCount());
        CHECK(ctlGuest, "① 양성대조: 딜레이 안 둘째 스코프는 그룹2(guest)로 태그에 기록된다");
        CHECK(ctlCount, "① 양성대조: guest 는 소독 횟수를 올리지 않는다(배치 1회)");
    }

    // ── ② 사장님 판정의 근거: 실패음 뒤 **2초 안에 다시 대면** host 를 되찾는다 ──
    bool reGuest = false, reStart = false;
    {
        as_disinfector();
        touch(mgr);
        const DateTime when = rel_date(11, 30, 0);
        rtc_set(when);
        start_committed(a, 0x25, 25, when);     // 커밋됨 + 리더는 host 를 모른다(방금 하드 리셋)
        sim_advance_ms(1200);                   // 사람이 실패음을 듣고 1.2초 뒤 다시 댄다(2초 창 안)
        logs_clear();
        touch(a);                               // → 종료가 아니라 **시작 재실행**
        const LocalDateTime s2 = get_ldt(a, SECTOR5_DISINFECTION_START);
        const LocalDateTime e2 = get_ldt(a, SECTOR6_DISINFECTION_END);
        // 종료 블록으로는 못 가른다 — 소독 시작은 **자동 종료를 미리 채운다**(그래서 같은 시에 종료가 찍힌다).
        //  가르는 것은 **시작 시각이 재접촉 시점으로 갱신됐는가**(원래 11:30:00 → 재접촉 11:30:01).
        reStart = (s2.Time.Hour == 11 && s2.Time.Minute == 30 && s2.Time.Second == 1);
        const uint8_t c0 = disinfectionOption.GetCount();
        sim_advance_ms(60UL * 1000);
        washed(b, 0x26, 26);
        touch(b);                               // 둘째 스코프
        reGuest = (group_of(b) == 2 && disinfectionOption.GetCount() == c0);
        tlog("  ② 재접촉(1.2초): 시작=%02u:%02u:%02u 종료시=%02u · b그룹=%d 횟수 %u->%u\n",
             s2.Time.Hour, s2.Time.Minute, s2.Time.Second, e2.Time.Hour, group_of(b),
             (unsigned)c0, (unsigned)disinfectionOption.GetCount());
        CHECK(reStart, "② 실패음 뒤 2초 안 재접촉은 '종료' 가 아니라 시작 재실행이다");
        CHECK(reGuest, "★② 그 재접촉이 host 를 되찾아 둘째 스코프가 guest(그룹2)로 기록된다 — 사장님 판정의 근거");
    }

    // ── ③ 대조: 다시 대지 않고 둘째 스코프를 대면 그룹1 둘이 된다(이 결함의 지문) ──
    {
        as_disinfector();
        touch(mgr);
        const DateTime when = rel_date(14, 0, 0);
        rtc_set(when);
        start_committed(a, 0x27, 27, when);
        const uint8_t c0 = disinfectionOption.GetCount();
        sim_advance_ms(60UL * 1000);
        washed(b, 0x28, 28);
        touch(b);                               // 다시 대지 않고 둘째만
        tlog("  ③ 대조(안 다시 댐): a그룹=%d b그룹=%d 횟수 %u->%u\n", group_of(a), group_of(b),
             (unsigned)c0, (unsigned)disinfectionOption.GetCount());
        // [10차 판정 · 사장님 09-27] 이 상태는 **실패음이 난 접촉 뒤에만** 생기고, 현장에서는 그 스코프를
        //  다시 대므로 ②로 낫는다. 고치려고 슬롯 확보를 커밋 앞으로 옮기면 나머지 41자리에 유령 host
        //  (과소 계수 = 오염된 액으로 계속 소독)가 생긴다 → 사실로만 잠근다.
        CHECK(group_of(b) == 1, "③ 지문(판정): 다시 대지 않으면 둘째 스코프도 그룹1 이 된다");
    }

    disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    done();
    for (;;) {}
}
