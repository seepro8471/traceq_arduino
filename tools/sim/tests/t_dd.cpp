// 9차 DD2 — 접촉 1초 미만(현장 사실)에서 **쓰기 도중 이탈**이 남기는 카드 상태의 **피해**를 잠근다.
//  L1 'Status=0 + 옛 환자' 태그가 한 주기를 돌아도 옛 환자가 PC 로 나가지 않는다(세척 시작이 비운다)
//  L2 세척 시작 절단은 어느 자리에서도 소독기에 '소독 종료' 로 읽히지 않는다(지난 주기 표지를 먼저 내린다)
//  L3 담당자 발급 절단 카드가 담당자로 등록되지 않는다(표지를 마지막에)
// ★상태가 아니라 **피해**를 본다 — 찢어진 카드 자체는 여전히 생긴다(막을 수 없다). 막는 것은 그 결과다.
#include "common.h"

static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}

// 게이트웨이가 찢겨 남는 상태를 **직접** 만든다(절단점이 있는지에 기대지 않는다):
//  Status=0(환자정보 없음) 인데 블록 8·9 에는 옛 환자가 살아 있다.
static void prior_patient_scope(SimCard &c, uint8_t uid, int no, uint8_t status, const char *key)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 0, 0, 0, 7, false, 0, 0});
    set_record(c, SECTOR1_GATEWAY, 7, rel_date(8, 0, 0));
    put_block(c, SECTOR2_PATIENT_KEY,  key, (uint8_t)strlen(key));
    put_block(c, SECTOR2_PATIENT_NAME, "OLDPATIENT", 10);
}

// 한 주기(세척→소독)를 돌고 서버 덤프. 반환 = 덤프에 그 키가 나갔는가.
static bool cycle_then_dump(SimCard &c)
{
    as_type('W');
    touch(c, 2, 4);                        // 세척 시작
    sim_advance_ms(20UL * 60 * 1000);
    touch(c, 2, 4);                        // 세척 종료
    as_type('D');
    touch(c, 2, 4);                        // 소독 시작
    sim_advance_ms(20UL * 60 * 1000);
    touch(c, 2, 4);                        // 소독 종료
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    logs_clear();
    serial_inject("Z", 1);
    touch(c, 2, 4);
    return serial_has("Ok!");
}

// 지난 주기(어제)가 태그에 그대로 남은 스코프 — 덤프를 안 한 현장. Rewrite=2 가 남으면 소독기가 '종료' 로 읽는다.
static void stale_cycle(SimCard &c)
{
    make_tag(c, 0x42, SCOPE_TYPE_TAG, 42, "SC0042", "S0042");
    set_process(c, Process{0, 1, 1, 1, 3, false, 0, 2});
    set_record(c, SECTOR2_WASHING_START,      3, yday(9, 0));
    set_record(c, SECTOR3_WASHING_END,        3, yday(9, 4));
    set_record(c, SECTOR5_DISINFECTION_START, 4, yday(9, 5));
    set_record(c, SECTOR6_DISINFECTION_END,   4, yday(9, 23));
}

static int tag_type(const SimCard &c)
{
    Company co{};
    memcpy(&co, c.data[SECTOR0_COMPANY], sizeof(co));
    return co.TagType;
}
static int tag_number(const SimCard &c)
{
    Tag t{};
    memcpy(&t, c.data[SECTOR0_TAG], sizeof(t));
    return t.Number;
}
// 담당자 발급은 번호가 아니라 **ID 칸**에 키를 넣는다(legacy_parse_manager_tag) — 신원은 이쪽으로 본다.
static bool tag_id_is(const SimCard &c, const char *s)
{
    Tag t{};
    memcpy(&t, c.data[SECTOR0_TAG], sizeof(t));
    return memcmp(t.ID, s, strlen(s)) == 0;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);
    const uint8_t dToday = rel_date(10, 0, 0).day();
    const uint8_t dYday  = yday(9, 0).day();

    // ── L1 'Status=0 + 옛 환자' 태그의 옛 환자가 PC 로 나가는가 ──
    //    OLDKEY99 = 4F 4C 44 4B 45 59 39 39 · REALKEY1 = 52 45 41 4C 4B 45 59 31
    bool sentOld = false, sentReal = false;
    {
        prior_patient_scope(sc, 0x41, 41, 0, "OLDKEY99");
        cycle_then_dump(sc);
        sentOld = serial_has("4F4C444B45593939");

        // 양성대조 — 환자정보가 **있는**(Status=1) 태그의 환자는 그대로 나가야 한다.
        //  이 줄이 없으면 "시험이 키를 아예 못 본다" 를 초록으로 착각한다.
        sim_advance_ms(60UL * 1000);
        prior_patient_scope(sc, 0x44, 44, 1, "REALKEY1");
        cycle_then_dump(sc);
        sentReal = serial_has("5245414C4B455931");
        tlog("  L1 옛 환자(Status=0) 전송=%u · 진짜 환자(Status=1) 전송=%u\n",
             (unsigned)sentOld, (unsigned)sentReal);
        CHECK(sentReal, "L1 양성대조: 환자정보 있는 태그의 등록번호는 덤프에 그대로 나간다");
        CHECK(!sentOld, "L1 'Status=0 + 옛 환자' 태그의 옛 등록번호는 덤프에 나가지 않는다(세척 시작이 비운다)");
    }

    // ── L2 세척 시작 절단점 전수: 지난 주기 표지가 남는 자리가 있는가 ──
    uint16_t K = 0;
    uint8_t nMixed = 0, nAsEnd = 0, checkedEnd = 0;
    bool ctlDisStart = false, ctlDisEnd = false;
    {
        as_type('W');
        stale_cycle(sc);
        sc.opCount = 0;
        touch(sc, 2, 4);
        K = sc.opCount;

        for (uint16_t n = 0; n <= K; ++n)
        {
            as_type('W');
            stale_cycle(sc);
            sc.removeAfterOps = (int16_t)n;
            sc.opCount = 0;
            logs_clear();
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            const Process p = get_process(sc);
            // 이번 주기 쓰기가 시작됐는데(블록 10 = 오늘) 지난 주기 표지가 남아 있는가
            const bool started = get_ldt(sc, SECTOR2_WASHING_START).Date.Day == dToday;
            if (!started) continue;
            // 지난 주기 표지 = Rewrite 2(소독 시작됨) · 소독 횟수 잔존. WashingStatus=1 은 **이번 주기 커밋**이
            // 세우는 값이라 그것만으로는 섞임이 아니다(첫 판에서 내 판정식이 커밋 성공을 섞임으로 셌다).
            if (p.Rewrite == 2 || p.DisinfectionCount != 0)
            {
                ++nMixed;
                if (nMixed <= 4)
                    tlog("   L2 섞임 n=%u ws=%u dc=%u rw=%u\n", (unsigned)n,
                         (unsigned)p.WashingStatus, (unsigned)p.DisinfectionCount, (unsigned)p.Rewrite);
            }
            // 대표 절단점 셋만 종단으로 — 소독기가 이 태그를 '종료' 로 기록하는가
            if (n == K / 3 || n == K / 2 || n == (2 * K) / 3)
            {
                ++checkedEnd;
                as_type('D');
                logs_clear();
                touch(sc, 2, 4);
                const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
                const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
                if (ds.Date.Day == dYday && de.Date.Day == dToday)
                {
                    ++nAsEnd;
                    tlog("   L2 종단 n=%u: 어제 시작 위에 오늘 종료가 써졌다\n", (unsigned)n);
                }
            }
        }
        tlog("  L2 K=%u · 섞인 절단점 %u · 종단 확인 %u 중 '안 한 소독 완료' %u\n",
             (unsigned)K, (unsigned)nMixed, (unsigned)checkedEnd, (unsigned)nAsEnd);

        // 양성대조 — 정상 흐름이 그대로 동작하는가(표지를 먼저 내린 것이 정상까지 죽이면 안 된다)
        as_type('W');
        stale_cycle(sc);
        touch(sc, 2, 4);                       // 온전한 세척 시작
        const Process cp = get_process(sc);
        as_type('D');
        const uint8_t c0 = disinfectionOption.GetCount();
        touch(sc, 2, 4);                       // 소독 시작
        const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
        ctlDisStart = (ds.Date.Day == dToday) && (disinfectionOption.GetCount() == (uint8_t)(c0 + 1));
        sim_advance_ms(20UL * 60 * 1000);
        touch(sc, 2, 4);                       // 소독 종료 — Rewrite==2 종료 갈래가 살아 있는가
        const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
        ctlDisEnd = (de.Date.Day == dToday) && (de.Time.Hour != 0 || de.Time.Minute != 0);
        tlog("  L2 대조: 커밋 뒤 rw=%u ws=%u dc=%u · 소독 시작 %u · 소독 종료 %u(%02u:%02u)\n",
             (unsigned)cp.Rewrite, (unsigned)cp.WashingStatus, (unsigned)cp.DisinfectionCount,
             (unsigned)ctlDisStart, (unsigned)ctlDisEnd, de.Time.Hour, de.Time.Minute);

        CHECK(K > 0, "L2 양성대조: 세척 시작 절단점 스윕이 실제로 돌았다");
        CHECK(ctlDisStart, "L2 양성대조: 온전한 세척 시작이면 소독기가 '시작' 으로 읽고 횟수가 오른다");
        CHECK(ctlDisEnd, "L2 양성대조: 온전한 소독 시작 뒤 재접촉은 '종료' 로 읽힌다(Rewrite=2 갈래 생존)");
        CHECK(nMixed == 0, "L2 세척 시작이 찢겨도 지난 주기 표지(Rewrite=2·소독 횟수)가 남지 않는다");
        CHECK(nAsEnd == 0, "L2 찢긴 세척 시작 뒤 소독기 접촉이 '안 한 소독'을 완료로 남기지 않는다");
    }

    // ── L3 레거시 담당자 발급 절단 — '종류만 담당자' 카드가 생기는가 ──
    uint8_t nHalf = 0;
    bool issued = false, bogus = false;
    {
        deviceOption.SetType('S');
        hard_reset(false, 2);
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        for (uint16_t n = 0; n <= 14; ++n)
        {
            make_tag(sc, 0x45, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
            set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
            sc.removeAfterOps = (int16_t)n;
            sc.opCount = 0;
            card_place(&sc);
            logs_clear();
            serial_inject("ZM7788;LEE;", 11);
            GUARDED(serialEvent());
            card_remove();
            run_loops(4);
            sc.removeAfterOps = 0;
            // '종류만 담당자' = 종류는 담당자인데 신원(ID)이 새 키가 아니다(옛 스코프 SC0021 이 남았다)
            if (tag_type(sc) == MANAGER_TYPE_TAG && !tag_id_is(sc, "7788"))
            {
                ++nHalf;
                tlog("   L3 종류만 담당자 n=%u (번호=%d ID옛스코프=%u)\n", (unsigned)n,
                     tag_number(sc), (unsigned)tag_id_is(sc, "SC0021"));
            }
        }
        // 양성대조 — 온전한 발급은 담당자 카드가 된다
        make_tag(sc, 0x46, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_place(&sc);
        logs_clear();
        serial_inject("ZM7788;LEE;", 11);
        GUARDED(serialEvent());
        card_remove();
        run_loops(4);
        issued = (tag_type(sc) == MANAGER_TYPE_TAG && tag_id_is(sc, "7788"));

        // 그 카드를 세척기에 대면 담당자로 등록된다(대조) — 스코프 ID 로는 등록되지 않아야 한다
        as_type('W');
        unsigned char k[ManagerOption::KEY_SIZE + 1]{};
        touch(sc, 2, 4);
        managerOption.GetKey(k, ManagerOption::KEY_SIZE);
        bogus = (memcmp(k, "SC0021", 6) == 0);
        tlog("  L3 종류만 담당자 %u자리 · 온전 발급=%u · 세척기 등록 키=%s\n",
             (unsigned)nHalf, (unsigned)issued, (const char *)k);
        CHECK(issued, "L3 양성대조: 온전한 담당자 발급은 종류와 신원(ID)이 함께 새것이 된다");
        CHECK(memcmp(k, "7788", 4) == 0, "L3 양성대조: 그 카드를 세척기에 대면 새 키로 등록된다");
        CHECK(nHalf == 0, "L3 발급이 찢겨도 '종류만 담당자 · 신원은 옛 스코프' 카드가 생기지 않는다");
        CHECK(!bogus, "L3 스코프 ID(SC0021)가 담당자로 등록되지 않는다");
    }

    done();
    for (;;) {}
}
