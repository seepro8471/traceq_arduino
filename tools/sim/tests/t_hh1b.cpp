// HH1(13회차) 두 번째 — v2.2.29 소거의 **순서**와 범위.
//  ⑤ 소거 도중 한 블록 쓰기가 실패하면(블록5 는 이미 0) 다음 접촉이 그 갈래를 건너뛰어 검사항목 잔재가 영구화되나
//  ⑥ 실제 접촉 이탈(removeAfterOps)로 같은 창에 들어가나 — 끊김 지점 전수
//  ⑦ Status==2(레거시) 건너뛰기: `update_process` 는 섹터15 의 블록 60 만 지운다 → 61·62 가 덤프로 나가나
//  ⑧ 잔재 소거가 실제로 도는 갈래의 접촉 예산(Status 0·1·3·200)
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}

// 지난 주기 잔재(검사 어제 08:00 ≤ 지난 세척 어제 09:00)를 가진 스코프
static void stale(SimCard &c, uint8_t uid, int no, uint8_t status)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 1, 1, 1, 1, false, 0, 2});
    set_record(c, SECTOR2_WASHING_START, 1, yday(9, 0));
    set_record(c, SECTOR3_WASHING_END, 1, yday(9, 4));
    set_record(c, SECTOR1_GATEWAY, 7, yday(8, 0));
    put_block(c, SECTOR15_EXAMINATION_SUBJECT,  "OLDSUB60", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUB61", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUB62", 8);
    put_block(c, SECTOR2_PATIENT_KEY,  "PKEY0001", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "PNAME001", 8);
}
static bool blk_is(const SimCard &c, uint8_t b, const char *s)
{
    return memcmp(c.data[b], s, strlen(s)) == 0;
}
static bool subj_any_old(const SimCard &c)
{
    return blk_is(c, SECTOR15_EXAMINATION_SUBJECT, "OLDSUB60") ||
           blk_is(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUB61") ||
           blk_is(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUB62");
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── ⑤ 소거 도중 블록60 쓰기만 거부 → 재접촉 ──
    {
        as_type('W');
        touch(mgr);
        stale(sc, 0x51, 51, 0);
        sc.nackBlock = SECTOR15_EXAMINATION_SUBJECT;       // 섹터15 첫 블록만 거부
        rtc_set(rel_date(10, 0, 0));
        logs_clear(); buzz_clear();
        touch(sc, 2, 4);
        const bool err1 = lcd_has("Write Error");
        const uint8_t rw1 = get_process(sc).Rewrite;
        const bool gwGone1 = get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
        const uint8_t b50 = buzz_count(50), b100 = buzz_count(100), b400 = buzz_count(400);
        tlog("  5a 1차(블록60 거부): 오류문구=%u RW=%u 블록5비움=%u · 소리 50x%u 100x%u 400x%u\n",
             (unsigned)err1, rw1, (unsigned)gwGone1,
             (unsigned)b50, (unsigned)b100, (unsigned)b400);
        // 소거는 이번 주기의 기록이 아니라 지난 잔재의 청소다 → 실패해도 세척 시작은 커밋되고 오류도 안 띄운다.
        CHECK(!err1 && rw1 == 1,
              "5a 잔재 소거가 실패해도 세척 시작은 오류 없이 커밋된다(소거 실패가 막힘이 되지 않는다)");
        // ★소리로도 안 드러난다 — 실패음(100×4)이 아니고 **그 태그의 환자정보 상태에 따른 소리**(여기선 Status=0
        //  이라 '환자정보 없음' 길게2)가 그대로 난다. 소거는 이번 주기의 기록이 아니므로 소리를 바꾸지 않는다.
        CHECK(b100 == 0 && b400 == 2 && b50 == 0,
              "5a2 잔재 소거 실패는 소리를 바꾸지 않는다 — 실패음이 아니고 그 태그의 원래 소리가 난다");
        // ★섹터15 를 못 지웠으면 판정 근거인 블록5 를 **남겨야** 한다 — 지우면 다음 접촉이 건너뛰어 잔재가 영구화된다.
        CHECK(!gwGone1,
              "5b 섹터15 소거가 실패하면 블록5(검사일시)를 지우지 않는다 — 다음 접촉이 다시 시도할 근거를 남긴다");

        // ★재접촉은 소거를 다시 하지 않는다 — 1차가 커밋됐으므로 다음 접촉은 세척 **종료**다.
        //  소거는 '세척 시작' 의 일이라 **다음 주기 세척 시작**에서 다시 시도된다. 그 사실을 잠근다.
        sc.nackBlock = -1;                                  // 손상이 아니라 한 번의 쓰기 실패였다
        rtc_set(rel_date(10, 20, 0));
        touch(sc, 2, 4);                                    // 세척 종료
        const bool leakThisCycle = subj_any_old(sc);         // 이 주기 덤프에는 옛 항목이 남는다(손상 대가)
        as_type('D');
        touch(mgr);
        rtc_set(rel_date(10, 30, 0));
        touch(sc, 2, 4);                                    // 소독 시작
        rtc_set(rel_date(10, 50, 0));
        touch(sc, 2, 4);                                    // 소독 종료
        as_type('W');
        touch(mgr);
        rtc_set(rel_date(12, 0, 0));
        logs_clear(); buzz_clear();
        touch(sc, 2, 4);                                    // 다음 주기 세척 시작
        tlog("  5c 이 주기 잔존=%u · 다음 주기 세척 시작 뒤 잔존=%u RW=%u 블록5비움=%u\n",
             (unsigned)leakThisCycle, (unsigned)subj_any_old(sc), get_process(sc).Rewrite,
             (unsigned)(get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0));
        CHECK(!subj_any_old(sc),
              "5c 소거가 한 번 실패해도 다음 주기 세척 시작이 옛 검사항목을 끝까지 비운다");
    }

    // ── ⑥ 실제 접촉 이탈 — 끊김 지점 전수(소거 구간에 걸리는 N 이 있나) ──
    {
        uint8_t nBad = 0, firstBad = 0, lastBad = 0;
        for (uint8_t n = 8; n <= 26; ++n)
        {
            as_type('W');
            touch(mgr);
            stale(sc, (uint8_t)(0x60 + (n & 0x0F)), 60 + n, 0);
            sc.removeAfterOps = (int16_t)n;
            rtc_set(rel_date(10, 0, 0));
            logs_clear();
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            rtc_set(rel_date(10, 0, 9));
            logs_clear();
            touch(sc, 2, 4);                                // 사람이 다시 댄다
            const bool bad = subj_any_old(sc) && get_process(sc).Rewrite == 1;
            if (bad) { ++nBad; if (!firstBad) firstBad = n; lastBad = n; }
        }
        tlog("  6 이탈 N=8..26: 잔재영구화 %u개 N=%u..%u\n",
             (unsigned)nBad, (unsigned)firstBad, (unsigned)lastBad);
        CHECK(nBad == 0,
              "6 접촉이 소거 도중 끊겨도 재접촉이 옛 검사항목을 끝까지 비운다");
    }

    // ── ⑦ Status==2 건너뛰기: update_process 는 블록 60 만 지운다 → 61·62 가 남아 덤프로 나가나 ──
    {
        as_type('W');
        touch(mgr);
        stale(sc, 0x57, 57, 2);
        rtc_set(rel_date(10, 0, 0));
        touch(sc, 2, 4);                                    // 세척 시작
        const bool b60 = blk_is(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUB60");
        const bool b61 = blk_is(sc, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUB61");
        rtc_set(rel_date(10, 20, 0));
        touch(sc, 2, 4);                                    // 세척 종료
        as_type('D');
        rtc_set(rel_date(10, 30, 0));
        touch(mgr);
        touch(sc, 2, 4);                                    // 소독 시작
        rtc_set(rel_date(10, 50, 0));
        touch(sc, 2, 4);                                    // 소독 종료
        deviceOption.SetType('S');
        hard_reset(false, 2);
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc, 2, 4);
        const bool ok = serial_has("Ok!");
        const bool leak61 = serial_has("4F4C445355423631");  // "OLDSUB61"
        const bool leak60 = serial_has("4F4C445355423630");  // "OLDSUB60"
        tlog("  7 Status2: 세척시작뒤 60잔존=%u 61잔존=%u · 덤프 Ok=%u 60나감=%u 61나감=%u\n",
             (unsigned)b60, (unsigned)b61, (unsigned)ok, (unsigned)leak60, (unsigned)leak61);
        CHECK(ok, "7a 전제: 한 주기 뒤 덤프가 Ok! 로 끝난다");
        CHECK(!leak61, "7b Status==2 갈래도 옛 검사항목(블록61)을 덤프로 내보내지 않는다");
    }

    // ── ⑧ 접촉 예산 — **정상 갈래**와 **잔재 소거 갈래**를 따로 잠근다 ──
    //    ⓐ 잔재 없음(검사일시 0 = 게이트웨이를 안 쓰는 현장) ⓑ 게이트웨이 정상(이번 검사 — 읽기만 늘어난다)
    //    ⓒ 잔재 소거가 실제로 도는 갈래. ⓒ 는 **태그마다 한 번뿐**이다 — 소거가 성공하면 블록5 가 0 이 되어
    //    다음 주기는 ⓐ 로 내려간다(업그레이드 직후 1회). 그래서 ⓐⓑ 는 t_ops 와 같은 40 으로 두고
    //    ⓒ 만 따로 50 으로 박는다. ★상한을 올려 덮지 않는다 — 정상 갈래가 무거워지면 ⓐⓑ 가 빨강이 된다.
    {
        uint16_t mxNormal = 0;
        for (uint8_t i = 0; i < 2; ++i)                      // ⓐ Status 0 / 1
        {
            as_type('W');
            touch(mgr);
            stale(sc, (uint8_t)(0x7E + i), 78 + i, (uint8_t)(i == 0 ? 0 : 1));
            memset(sc.data[SECTOR1_GATEWAY], 0, 16);
            rtc_set(rel_date(10, 0, 0));
            sc.opCount = 0;
            touch(sc, 2, 4);
            if (sc.opCount > mxNormal) mxNormal = sc.opCount;
            tlog("  8a 잔재 없음 Status=%u → 동작 %u\n", (unsigned)(i == 0 ? 0 : 1), sc.opCount);
        }
        for (uint8_t i = 0; i < 2; ++i)                      // ⓑ 게이트웨이 정상 — 검사일시가 이번 검사
        {
            as_type('W');
            touch(mgr);
            stale(sc, (uint8_t)(0x6E + i), 68 + i, (uint8_t)(i == 0 ? 0 : 1));
            set_record(sc, SECTOR1_GATEWAY, 7, rel_date(8, 0, 0));
            rtc_set(rel_date(10, 0, 0));
            sc.opCount = 0;
            touch(sc, 2, 4);
            if (sc.opCount > mxNormal) mxNormal = sc.opCount;
            tlog("  8b 게이트웨이 정상 Status=%u → 동작 %u · 검사일시보존=%u\n",
                 (unsigned)(i == 0 ? 0 : 1), sc.opCount,
                 (unsigned)(get_ldt(sc, SECTOR1_GATEWAY).Date.Year != 0));
        }
        CHECK(mxNormal > 0 && mxNormal <= 40,
              "8a 잔재가 없는 세척 시작(게이트웨이 유무 무관)은 접촉 예산 40 안 — t_ops 와 같은 상한");

        const uint8_t sv[4] = {0, 1, 3, 200};
        uint16_t mx = 0;
        for (uint8_t i = 0; i < 4; ++i)
        {
            as_type('W');
            touch(mgr);
            stale(sc, (uint8_t)(0x70 + i), 70 + i, sv[i]);
            rtc_set(rel_date(10, 0, 0));
            sc.opCount = 0;
            touch(sc, 2, 4);
            const uint16_t ops = sc.opCount;
            if (ops > mx) mx = ops;
            tlog("  8c Status=%3u 잔재 소거 → 동작 %u · 검사일시비움=%u\n", sv[i], ops,
                 (unsigned)(get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0));
        }
        CHECK(mx > mxNormal && mx <= 50,
              "8c 잔재 소거가 도는 세척 시작은 50 안 — 태그마다 한 번뿐인 비용(정상 갈래보다 무겁다)");
    }

    // ── ⑫ 접촉 이탈 지점 전수 — **이번 검사**가 지워지는 N 이 몇 개인가(P1-1 의 빈도) ──
    //    태그: 지난 주기 세척 = 어제 · 검사 = 오늘 08:00(이번 검사) · Status=1(게이트웨이가 환자정보까지 씀)
    {
        uint8_t nLost = 0, firstLost = 0, lastLost = 0, nRun = 0;
        for (uint8_t n = 8; n <= 40; ++n)
        {
            as_type('W');
            touch(mgr);
            char id[10], ser[10];
            snprintf(id, sizeof(id), "SC%04d", 100 + n);
            snprintf(ser, sizeof(ser), "S%04d", 100 + n);
            make_tag(sc, (uint8_t)(0x80 + (n & 0x1F)), SCOPE_TYPE_TAG, 100 + n, id, ser);
            set_process(sc, Process{1, 1, 1, 1, 1, false, 0, 2});
            set_record(sc, SECTOR2_WASHING_START, 1, yday(9, 0));
            set_record(sc, SECTOR3_WASHING_END, 1, yday(9, 4));
            set_record(sc, SECTOR1_GATEWAY, 7, rel_date(8, 0, 0));      // 이번 검사
            put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "THISEXAM", 8);
            sc.removeAfterOps = (int16_t)n;
            rtc_set(rel_date(10, 0, 0));
            logs_clear();
            touch(sc, 2, 4);                                            // 접촉이 n동작 뒤 끊긴다
            sc.removeAfterOps = 0;
            rtc_set(rel_date(10, 0, 9));
            logs_clear();
            touch(sc, 2, 4);                                            // 사람이 다시 댄다
            ++nRun;
            const bool lost = !blk_is(sc, SECTOR15_EXAMINATION_SUBJECT, "THISEXAM") ||
                              get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
            if (lost) { ++nLost; if (!firstLost) firstLost = n; lastLost = n; }
        }
        tlog("  12 이탈 N=8..40(%u개): 이번 검사 유실 %u개 N=%u..%u\n",
             (unsigned)nRun, (unsigned)nLost, (unsigned)firstLost, (unsigned)lastLost);
        CHECK(nLost == 0, "12 접촉이 어디서 끊겨도 재접촉이 이번 검사의 검사일시·검사항목을 지우지 않는다");
    }

    done();
    for (;;) {}
}
