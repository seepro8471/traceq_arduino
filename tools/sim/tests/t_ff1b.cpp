// FF1b — 10차 ① `isRestart` 가 **참이어야 하는데 거짓 / 거짓이어야 하는데 참** 인 갈래 전수.
//  H1 연속 더블터치 3회 — 매 회 '시작 재실행' 인가 · 동작 수 · 표지
//  H2 재시작 경로에 지난 주기 표지(Rewrite=2·소독 횟수)가 살아남는 자리가 있나(찢김 전수)
//  H3 is_valid 읽기 실패 뒤 재접촉 — 판정이 흔들리나
//  H4 일회성 담당자 현장의 재시작이 담당자 블록을 **빈 값으로** 덮나
//  H5 다른 세척기에서의 재시작 — 기기번호·표지
//  H6 게이트웨이 환자(Status=1) 태그의 재시작이 환자 블록을 비우나
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }

static void as_washer(int number)
{
    deviceOption.SetType('W');
    deviceOption.SetNumber(number);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}

static void fresh_scope(SimCard &c, uint8_t uid, uint8_t status)
{
    make_tag(c, uid, SCOPE_TYPE_TAG, 71, "SC0071", "S0071");
    set_process(c, Process{status, 0, 0, 0, 0, false, 0, 0});
}

// 세척 종료 - 세척 시작 (초). 재시작이면 자동 종료 미리채움(≈세척시간)이라 4분대.
static int32_t wash_span(const SimCard &c)
{
    return (DefaultRtc::ToDateTime(get_ldt(c, SECTOR3_WASHING_END)) -
            DefaultRtc::ToDateTime(get_ldt(c, SECTOR2_WASHING_START))).totalseconds();
}
static bool mgr_block_blank(const SimCard &c, uint8_t blk)
{
    for (uint8_t i = 0; i < 14; ++i) if (c.data[blk][i] != 0) return false;
    return true;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    managerOption.SetData(mk, mn);

    // ── H1 연속 더블터치 3회 ──
    {
        as_washer(1);
        fresh_scope(sc, 0x71, 1);           // Status=1 → '환자정보 없음' 안내(400ms×2)가 없어 2초 창 안
        touch(sc, 1, 4);
        const LocalDateTime s1 = get_ldt(sc, SECTOR2_WASHING_START);
        sc.opCount = 0;
        touch(sc, 1, 4);
        const uint16_t k2 = sc.opCount;
        const LocalDateTime s2 = get_ldt(sc, SECTOR2_WASHING_START);
        sc.opCount = 0;
        touch(sc, 1, 4);
        const uint16_t k3 = sc.opCount;
        const LocalDateTime s3 = get_ldt(sc, SECTOR2_WASHING_START);
        const Process p = get_process(sc);
        const int32_t sp = wash_span(sc);
        tlog("  H1 시작 %02u:%02u:%02u -> %02u:%02u:%02u -> %02u:%02u:%02u · 동작 2차 %u 3차 %u · ws=%u rw=%u dc=%u · 구간 %ld초\n",
             s1.Time.Hour, s1.Time.Minute, s1.Time.Second, s2.Time.Hour, s2.Time.Minute, s2.Time.Second,
             s3.Time.Hour, s3.Time.Minute, s3.Time.Second, (unsigned)k2, (unsigned)k3,
             (unsigned)p.WashingStatus, (unsigned)p.Rewrite, (unsigned)p.DisinfectionCount, (long)sp);
        CHECK(p.WashingStatus == 1 && p.Rewrite == 1 && p.DisinfectionCount == 0,
              "H1 더블터치 3회 뒤에도 표지는 '세척 시작' 그대로다");
        CHECK(sp >= 4L * 60 && sp < 5L * 60,
              "H1 3회째도 '종료' 가 아니라 시작 재실행이다(자동 종료가 세척시간만큼 앞서 채워진다)");
        CHECK(k2 <= 40 && k3 <= 40, "H1 재시작 접촉도 예산(40) 안");
        CHECK(k3 == k2, "H1 2차·3차 재시작의 동작 수가 같다(경로가 갈리지 않는다)");
    }

    // ── H2 재시작 찢김 전수: 지난 주기 표지가 살아남는 자리 ──
    //    ★2.0 이 만들 수 없는 조합(Rewrite=1 인데 소독 횟수 1)을 **직접** 만들어 사실만 잰다.
    //     Rewrite=1 을 쓰는 자리는 update_process 하나이고 그것이 소독 횟수를 0 으로 쓰므로
    //     제품 흐름으로는 이 상태가 생기지 않는다 — 1.0 기기와 섞인 현장을 가정한 사실 측정.
    uint16_t K = 0;
    uint8_t nStale = 0, nAsEnd = 0, checked = 0;
    {
        as_washer(1);
        fresh_scope(sc, 0x72, 1);
        touch(sc, 1, 4);
        sc.opCount = 0;
        touch(sc, 1, 4);
        K = sc.opCount;

        for (uint16_t n = 0; n <= K; ++n)
        {
            as_washer(1);
            fresh_scope(sc, 0x72, 1);
            touch(sc, 1, 4);                                  // 1차 세척 시작(커밋)
            // 1.0 이 남긴 모습으로 바꿔 둔다: Rewrite=1 + 지난 주기 소독 기록
            Process q = get_process(sc);
            if (q.Rewrite != 1) continue;
            q.DisinfectionCount = 1;
            q.DisinfectionStatus = 1;
            set_process(sc, q);
            set_record(sc, SECTOR5_DISINFECTION_START, 4, yday(9, 5));
            set_record(sc, SECTOR6_DISINFECTION_END,   4, yday(9, 23));
            sc.removeAfterOps = (int16_t)n;
            sc.opCount = 0;
            logs_clear();
            touch(sc, 1, 4);                                  // 재시작이 n동작째에 끊긴다
            sc.removeAfterOps = 0;
            const Process p = get_process(sc);
            if (p.Rewrite == 2 || p.DisinfectionCount != 0)
            {
                ++nStale;
                if (nStale <= 3)
                    tlog("   H2 잔존 n=%u ws=%u dc=%u rw=%u\n", (unsigned)n,
                         (unsigned)p.WashingStatus, (unsigned)p.DisinfectionCount, (unsigned)p.Rewrite);
                if (checked < 3 && (n == K / 4 || n == K / 2 || n == (3 * K) / 4))
                {
                    ++checked;
                    deviceOption.SetType('D');
                    hard_reset(false, 2);
                    managerOption.SetData(mk, mn);
                    logs_clear();
                    touch(sc, 2, 4);
                    // ★소독 '시작'은 자동 종료를 미리 채우므로 종료 블록만으로는 시작/종료를 가를 수 없다
                    //  (SCOPE10 정정). 가르는 것은 **시작이 어제 그대로인데 종료가 오늘**인가다(t_dd L2 와 같은 식).
                    const LocalDateTime ds = get_ldt(sc, SECTOR5_DISINFECTION_START);
                    const LocalDateTime de = get_ldt(sc, SECTOR6_DISINFECTION_END);
                    if (ds.Date.Day == yday(9, 5).day() && de.Date.Day == rel_date(10, 0, 0).day())
                        ++nAsEnd;
                }
            }
        }
        tlog("  H2 K=%u · 지난 주기 표지 잔존 %u / %u · 종단 %u 중 '안 한 소독 완료' %u\n",
             (unsigned)K, (unsigned)nStale, (unsigned)(K + 1), (unsigned)checked, (unsigned)nAsEnd);
        CHECK(K > 0, "H2 양성대조: 재시작 찢김 스윕이 실제로 돌았다");
        CHECK(nAsEnd == 0,
              "H2 재시작이 찢겨도 '안 한 소독'이 소독기에서 완료로 기록되지 않는다");
    }

    // ── H3 is_valid 읽기 실패 뒤 재접촉 ──
    {
        as_washer(1);
        fresh_scope(sc, 0x73, 1);
        touch(sc, 1, 4);                                      // 1차 시작(커밋)
        const Process p1 = get_process(sc);
        const LocalDateTime s1 = get_ldt(sc, SECTOR2_WASHING_START);
        sc.readErrBlock = SECTOR1_PROCESS;                    // is_valid 의 read_process 를 실패시킨다
        sc.readErrTimes = 4;
        logs_clear();
        sc.opCount = 0;
        touch(sc, 1, 4);
        const uint16_t kErr = sc.opCount;
        sc.readErrBlock = -1;
        sc.readErrTimes = 0;
        const Process p2 = get_process(sc);
        const LocalDateTime s2 = get_ldt(sc, SECTOR2_WASHING_START);
        const bool sameAfterErr = (p2.WashingStatus == p1.WashingStatus && p2.Rewrite == p1.Rewrite &&
                                   ldt_eq(s2, s1.Date.Year, s1.Date.Month, s1.Date.Day,
                                          s1.Time.Hour, s1.Time.Minute, s1.Time.Second));
        logs_clear();
        touch(sc, 1, 4);                                      // 회복 접촉(읽기 정상)
        const Process p3 = get_process(sc);
        tlog("  H3 읽기실패 접촉: 동작 %u 불변=%u(안내 ReadError=%u) · 회복 접촉 뒤 ws=%u rw=%u 구간 %ld초\n",
             (unsigned)kErr, (unsigned)sameAfterErr, (unsigned)lcd_has("Read Error"),
             (unsigned)p3.WashingStatus, (unsigned)p3.Rewrite, (long)wash_span(sc));
        CHECK(kErr > 0, "H3 양성대조: 읽기 실패 접촉에서도 카드가 실제로 읽혔다(주입이 먹었다)");
        CHECK(sameAfterErr, "H3 is_valid 읽기 실패 접촉은 태그를 하나도 바꾸지 않는다");
        CHECK(p3.WashingStatus == 1 && p3.Rewrite == 1,
              "H3 읽기 실패 뒤 회복 접촉도 표지를 잃지 않는다");
    }

    // ── H4 일회성 담당자 + 재시작 — 담당자 블록이 빈 값으로 덮이나 ──
    {
        as_washer(1);
        make_tag(mgr, 0x06, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
        fresh_scope(sc, 0x74, 1);
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                           // 일회성 담당자 등록
        touch(sc, 1, 4);                                      // 1차 시작 — 일회성 소모
        const bool blank1 = mgr_block_blank(sc, SECTOR3_WASHING_START_MANAGER_KEY);
        logs_clear();
        touch(sc, 1, 4);                                      // 2초 안 재접촉 = 재시작
        const bool blank2 = mgr_block_blank(sc, SECTOR3_WASHING_START_MANAGER_KEY);
        const Process p = get_process(sc);
        tlog("  H4 1차 담당자 빔=%u · 재시작 뒤 담당자 빔=%u([%.8s]) · ws=%u rw=%u 거부=%u\n",
             (unsigned)blank1, (unsigned)blank2,
             (const char *)sc.data[SECTOR3_WASHING_START_MANAGER_KEY],
             (unsigned)p.WashingStatus, (unsigned)p.Rewrite, (unsigned)lcd_has("No Manager Info"));
        recordOption.SetManagerDisposability(false);
        CHECK(!blank1, "H4 양성대조: 일회성 담당자로 1차 시작의 담당자 블록이 채워진다");
        CHECK(!blank2, "H4 일회성이 소모된 뒤의 재시작이 담당자 블록을 빈 값으로 덮지 않는다");
    }

    // ── H5 다른 세척기에서의 재시작 ──
    {
        as_washer(1);
        fresh_scope(sc, 0x75, 1);
        touch(sc, 1, 4);
        as_washer(5);                                         // 2호기로 (같은 2초 안)
        logs_clear();
        touch(sc, 1, 4);
        const Process p = get_process(sc);
        tlog("  H5 재시작 뒤 기기번호=%d ws=%u rw=%u dc=%u 구간 %ld초\n",
             p.MachineNumber, (unsigned)p.WashingStatus, (unsigned)p.Rewrite,
             (unsigned)p.DisinfectionCount, (long)wash_span(sc));
        CHECK(p.WashingStatus == 1 && p.Rewrite == 1 && p.MachineNumber == 5,
              "H5 다른 세척기에서의 재시작도 표지를 지키고 기기번호를 그 기기로 갱신한다");
    }

    // ── H6 Status=1(게이트웨이 환자) 태그의 재시작이 환자 블록을 비우나 ──
    {
        as_washer(1);
        make_tag(sc, 0x76, SCOPE_TYPE_TAG, 76, "SC0076", "S0076");
        set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});
        put_block(sc, SECTOR2_PATIENT_KEY,  "PT0076", 6);
        put_block(sc, SECTOR2_PATIENT_NAME, "REALNAME", 8);
        touch(sc, 1, 4);
        const bool keep1 = memcmp(sc.data[SECTOR2_PATIENT_KEY], "PT0076", 6) == 0;
        touch(sc, 1, 4);
        const bool keep2 = memcmp(sc.data[SECTOR2_PATIENT_KEY], "PT0076", 6) == 0;
        const Process p = get_process(sc);
        tlog("  H6 1차 환자유지=%u · 재시작 뒤 환자유지=%u([%.8s]) Status=%u\n",
             (unsigned)keep1, (unsigned)keep2,
             (const char *)sc.data[SECTOR2_PATIENT_KEY], (unsigned)p.Status);
        CHECK(keep1 && keep2 && p.Status == 1,
              "H6 환자정보 있는 태그는 1차 시작·재시작 어느 쪽에서도 환자 블록을 잃지 않는다");
    }

    done();
    for (;;) {}
}
