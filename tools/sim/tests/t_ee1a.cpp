// EE1a — 9회차 ① `washing_start` 머리의 선행 쓰기 둘이 **무엇을 새로 깨뜨렸나**.
//  W1 이미 커밋된 이번 주기 세척 시작 위에 **더블터치 재시작**이 찢기면 그 주기가 사라지는가
//     (선행 쓰기가 WashingStatus·Rewrite 를 먼저 0 으로 내리므로)
//  W2 지난 주기(미덤프) 태그의 세척 시작이 찢기면 그 지난 주기를 서버가 아직 받아 주는가
//  W3 선행 소거의 값 — Status=0 / Status=1 세척 시작의 카드 동작 수
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

static void fresh_scope(SimCard &c, uint8_t uid, uint8_t status)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
    set_process(c, Process{status, 0, 0, 0, 0, false, 0, 0});
}

// 지난 주기(어제)가 태그에 그대로 — 덤프를 안 한 현장
static void stale_cycle(SimCard &c, uint8_t uid)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, 42, "SC0042", "S0042");
    set_process(c, Process{0, 1, 1, 1, 3, false, 0, 2});
    set_record(c, SECTOR2_WASHING_START,      3, yday(9, 0));
    set_record(c, SECTOR3_WASHING_END,        3, yday(9, 4));
    set_record(c, SECTOR5_DISINFECTION_START, 4, yday(9, 5));
    set_record(c, SECTOR6_DISINFECTION_END,   4, yday(9, 23));
}

// 세척 종료 - 세척 시작 (초). 재시작이면 자동 종료 미리채움(≈세척시간)이라 4분대, 진짜 종료면 몇 초.
static int32_t wash_span(const SimCard &c)
{
    return (DefaultRtc::ToDateTime(get_ldt(c, SECTOR3_WASHING_END)) -
            DefaultRtc::ToDateTime(get_ldt(c, SECTOR2_WASHING_START))).totalseconds();
}

// 서버에 대면 `Ok!` 가 나가는가(= PC 가 그 주기를 저장할 수 있는가)
static bool dumpable(SimCard &c)
{
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

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);

    // ── W1 더블터치 재시작 절단 ──
    uint16_t K = 0;
    uint8_t nNoProcess = 0, checked = 0, nRejected = 0;
    bool ctlRestart = false;
    {
        // Status=1(게이트웨이 사용) 표본 — '환자정보 없음' 안내(400ms×2)가 안 나 두 접촉 간격이 2초 창 안에 든다
        as_type('W');
        fresh_scope(sc, 0x21, 1);
        touch(sc, 1, 4);                       // 1차 세척 시작(커밋)
        sc.opCount = 0;
        touch(sc, 1, 4);                       // 2초 안 재접촉 = 시작 재실행
        K = sc.opCount;
        const Process cp = get_process(sc);
        const int32_t sp = wash_span(sc);
        ctlRestart = (cp.WashingStatus == 1 && cp.Rewrite == 1 && sp >= 4L * 60 && sp < 5L * 60);
        tlog("  W1 K=%u · 온전 더블터치 뒤 ws=%u rw=%u · 세척구간 %ld초(재시작이면 4분대)\n",
             (unsigned)K, (unsigned)cp.WashingStatus, (unsigned)cp.Rewrite, (long)sp);

        for (uint16_t n = 0; n <= K; ++n)
        {
            as_type('W');
            fresh_scope(sc, 0x21, 1);
            touch(sc, 1, 4);                   // 1차 세척 시작(온전 — 커밋됨)
            const Process before = get_process(sc);
            if (before.WashingStatus != 1 || before.Rewrite != 1) continue;
            sc.removeAfterOps = (int16_t)n;
            sc.opCount = 0;
            logs_clear();
            touch(sc, 1, 4);                   // 더블터치가 n동작째에 끊긴다
            sc.removeAfterOps = 0;
            const Process p = get_process(sc);
            if (p.WashingStatus == 0 && p.Rewrite == 0)
            {
                ++nNoProcess;
                if (nNoProcess <= 3)
                    tlog("   W1 공정소실 n=%u ws=%u rw=%u dc=%u\n", (unsigned)n,
                         (unsigned)p.WashingStatus, (unsigned)p.Rewrite, (unsigned)p.DisinfectionCount);
                if (checked < 3 && (n == K / 4 || n == K / 2 || n == (3 * K) / 4))
                {
                    ++checked;
                    as_type('D');
                    logs_clear();
                    touch(sc, 2, 4);
                    if (lcd_has("No Washing Info")) ++nRejected;
                }
            }
        }
        tlog("  W1 공정 소실 절단점 %u / %u · 종단 %u 중 소독기 거부 %u\n",
             (unsigned)nNoProcess, (unsigned)(K + 1), (unsigned)checked, (unsigned)nRejected);

        CHECK(K > 0, "W1 양성대조: 더블터치 재시작 스윕이 실제로 돌았다");
        CHECK(ctlRestart, "W1 양성대조: 온전한 더블터치는 '시작 재실행' 으로 처리된다(표지·자동종료 재기록)");
        CHECK(nNoProcess == 0,
              "W1 더블터치 재시작이 찢겨도 이미 커밋된 이번 주기 세척 시작이 사라지지 않는다");
    }

    // ── W2 지난 주기(미덤프) 태그 — 세척 시작 절단 뒤에도 그 주기를 서버가 받아 주는가 ──
    uint8_t nDumpable = 0, samples = 0;
    bool ctlStaleDump = false;
    {
        as_type('W');
        stale_cycle(sc, 0x42);
        sc.opCount = 0;
        touch(sc, 2, 4);
        const uint16_t KW = sc.opCount;

        stale_cycle(sc, 0x42);
        ctlStaleDump = dumpable(sc);

        const uint16_t pts[3] = {(uint16_t)(KW / 4), (uint16_t)(KW / 2), (uint16_t)((3 * KW) / 4)};
        for (uint8_t i = 0; i < 3; ++i)
        {
            as_type('W');
            stale_cycle(sc, 0x42);
            sc.removeAfterOps = (int16_t)pts[i];
            sc.opCount = 0;
            logs_clear();
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            ++samples;
            if (dumpable(sc)) ++nDumpable;
        }
        tlog("  W2 KW=%u · 손대지 않은 지난 주기 덤프=%u · 찢긴 뒤 덤프 가능 %u/%u (절단점 %u·%u·%u)\n",
             (unsigned)KW, (unsigned)ctlStaleDump, (unsigned)nDumpable, (unsigned)samples,
             (unsigned)pts[0], (unsigned)pts[1], (unsigned)pts[2]);
        CHECK(ctlStaleDump, "W2 양성대조: 지난 주기가 남은 태그는 서버가 받아 준다");
        // [10차 판정] 선행 소거의 **대가**다 — 찢긴 세척 시작은 아직 안 올린 지난 주기를 덤프 불가로 만든다.
        //  안 내리면 "안 한 소독이 완료" 로 대장에 남고(9회차 P1) 그쪽이 더 나쁘다. 성공한 세척 시작도 그
        //  주기를 어차피 덮으므로 잃는 것은 "찢긴 시도에서 몇 초 일찍" 뿐이다. 되돌리기로 결정하면 이 줄이 걸린다.
        CHECK(nDumpable == 0,
              "W2 판정: 찢긴 세척 시작은 아직 안 올린 지난 주기를 덤프 불가로 만든다(선행 소거의 대가)");
    }

    // ── W3 선행 소거의 값 — Status 별 카드 동작 수 ──
    {
        as_type('W');
        fresh_scope(sc, 0x23, 0);
        sc.opCount = 0;
        touch(sc, 2, 4);
        const uint16_t op0 = sc.opCount;

        as_type('W');
        fresh_scope(sc, 0x24, 1);
        sc.opCount = 0;
        touch(sc, 2, 4);
        const uint16_t op1 = sc.opCount;
        tlog("  W3 세척 시작 동작 수 — Status=0(게이트웨이 미사용) %u · Status=1 %u · 차 %d\n",
             (unsigned)op0, (unsigned)op1, (int)op0 - (int)op1);
        CHECK(op0 > 0 && op1 > 0, "W3 양성대조: 두 갈래 모두 동작이 세어졌다");
        CHECK(op0 <= 40 && op1 <= 40, "W3 두 갈래 모두 t_ops 상한(40) 안");
    }

    done();
    for (;;) {}
}
