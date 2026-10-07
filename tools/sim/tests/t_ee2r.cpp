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
    deviceOption.SetNumber(2);                // 표본(start_committed)의 시작 기기 = 이 기기 — 16차: 재시작은 같은 기기에서만
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

    // ── ② 사장님 판정의 근거: 실패음 뒤 **10초 안에 다시 대면**(15차 A3) host 를 되찾는다 ──
    bool reGuest = false, reStart = false;
    {
        as_disinfector();
        touch(mgr);
        const DateTime when = rel_date(11, 30, 0);
        rtc_set(when);
        start_committed(a, 0x25, 25, when);     // 커밋됨 + 리더는 host 를 모른다(방금 하드 리셋)
        const uint8_t cBefore = disinfectionOption.GetCount();
        sim_advance_ms(1200);                   // 사람이 실패음을 듣고 1.2초 뒤 다시 댄다(창 10초 안 · 15차 A3)
        logs_clear();
        touch(a);                               // → 종료가 아니라 **시작 재실행**
        // 16차: 재시작인데 RAM 이 이 스코프를 몰랐다 = 앞 시도의 커밋 뒤 부수효과(횟수)가 건너뛰어진 것 → 여기서 올린다(종전 −1 영구)
        CHECK(disinfectionOption.GetCount() == cBefore + 1,
              "★②(16차) 확인 실패 뒤 재시작이 앞 시도의 빠진 소독 횟수를 올린다(RAM 이 모르는 재시작 = 찢긴 커밋)");
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
        CHECK(reStart, "② 실패음 뒤 10초 안 재접촉은 '종료' 가 아니라 시작 재실행이다");
        CHECK(reGuest, "★② 그 재접촉이 host 를 되찾아 둘째 스코프가 guest(그룹2)로 기록된다 — 사장님 판정의 근거");
    }

    // ── ②r (17차 V-J R-1) 같은 것을 **실제 찢긴 시작**으로 — 위 ② 는 상태를 손으로 만들어 "횟수는 커밋 뒤에 올린다" 순서를 못 잠갔다
    //  (IncrementCount 를 커밋 앞으로 옮기면 찢긴 시작 +1 · 재시작 +1 = +2 인데 ② 는 초록). 실제 찢김 + 재접촉 = 정확히 +1.
    {
        as_disinfector();
        touch(mgr);
        rtc_set(rel_date(12, 30, 0));
        washed(a, 0x27, 27);
        const uint8_t cBefore = disinfectionOption.GetCount();
        a.readErrBlock = SECTOR1_PROCESS; a.readErrSkip = 1; a.readErrTimes = 30;   // 커밋은 닿고 확인 읽기 실패
        logs_clear(); touch(a, 1, 4);
        a.readErrBlock = -1; a.readErrTimes = 0; a.readErrSkip = 0;
        const bool torn = lcd_has("Write Error") && get_process(a).Rewrite == 2;
        const uint8_t cTorn = disinfectionOption.GetCount();
        sim_advance_ms(1200);
        logs_clear(); touch(a);                                                     // 재시작(RAM 은 a 를 모른다)
        tlog("  ②r 실제 찢김: torn=%d 횟수 %u -> %u -> %u\n", torn, (unsigned)cBefore, (unsigned)cTorn, (unsigned)disinfectionOption.GetCount());
        CHECK(torn && cTorn == cBefore, "②r 전제: 커밋은 닿았고 확인 실패로 Write Error — 그 접촉은 횟수를 안 올렸다(횟수는 커밋 뒤)");
        CHECK(disinfectionOption.GetCount() == cBefore + 1,
              "②r(17차) 실제 찢긴 시작 + 재접촉 = 횟수 정확히 +1 — 횟수를 커밋 앞으로 옮기면 +2 가 된다(순서 잠금)");
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
        //  다시 대므로 ②로 낫는다. 고치려고 슬롯 확보를 커밋 앞으로 옮기면 나머지 35자리(11차 실측)에 유령 host
        //  (과소 계수 = 오염된 액으로 계속 소독)가 생긴다 → 사실로만 잠근다.
        CHECK(group_of(b) == 1, "③ 지문(판정): 다시 대지 않으면 둘째 스코프도 그룹1 이 된다");
    }

    disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    done();
    for (;;) {}
}
