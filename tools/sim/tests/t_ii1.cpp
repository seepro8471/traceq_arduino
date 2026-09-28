// 14회차 II-B 잠금 — washing_start 잔재 판정은 표지만(사장님 선택 1): 공정 전부 0 + Status 0 + 검사일시 있음 → 지운다.
//  시간 비교 없음 · 덤프 전(공정 ≠ 0)은 안 지운다.
//  A. 커밋(블록6) 창에서 찢긴 시작 → 9초/5분 뒤 다시 댐 → 이번 검사(Status 1)가 지워지나
//     (옛 시간 판정은 앞 시도가 미리채운 블록14 를 '지난 주기 종료' 로 읽었다 — 지금은 블록14 를 안 읽는다)
//  B. 같은 주기 재세척(검사 → 세척 → 소독 → 덤프 전 재세척) → 이번 주기 검사정보가 지워지나
//  C. slot1 = 0 이면 즉시 재접촉(9초)도 지우나
//  D. 덤프 뒤 게이트웨이를 안 거친 어제 검사(Status 0)는 지운다 · E. D 에 찢김 재접촉을 겹친 사실 기록(CHECK 없음)
//  모든 태그 상태는 **현장에서 도달 가능한 것**만: 덤프 뒤(Process{}) → 게이트웨이(Status=1) 상태에서 출발.
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }
static DateTime at(uint8_t h, uint8_t m, uint8_t s) { return rel_date(h, m, s); }

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}

// 덤프(Process{} · 블록5·15·8·9 소거) 뒤 게이트웨이가 오늘 09:00 검사를 기록한 태그 — 도달 가능한 상태 그대로.
static void after_gateway(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 0, 0, 0, 0, false, 0, 0});      // 덤프 커밋 Process{} 뒤 게이트웨이가 Status=1
    set_record(c, SECTOR2_WASHING_START, 1, yday(9, 0));       // 지난 주기(어제) 세척 — 덤프는 안 지운다
    set_record(c, SECTOR3_WASHING_END, 1, yday(9, 4));
    set_record(c, SECTOR1_GATEWAY, 7, at(9, 0, 0));            // 오늘 검사
    put_block(c, SECTOR15_EXAMINATION_SUBJECT, "TODAYSUB", 8);
    put_block(c, SECTOR2_PATIENT_KEY, "PKEY0001", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "PNAME001", 8);
}
static bool exam_kept(const SimCard &c)
{
    return memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT], "TODAYSUB", 8) == 0 &&
           get_ldt(c, SECTOR1_GATEWAY).Date.Year != 0;
}

int main()
{
    rtc_set(at(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── 0 전제: 한 번에 끝나는 세척 시작의 동작 수 ──
    uint16_t total = 0;
    {
        as_type('W');
        touch(mgr);
        after_gateway(sc, 0x40, 40);
        rtc_set(at(10, 0, 0));
        sc.opCount = 0;
        touch(sc, 2, 4);
        total = sc.opCount;
        tlog("  0 정상 시작: 동작 %u · RW=%u 검사보존=%u\n", total, get_process(sc).Rewrite, (unsigned)exam_kept(sc));
        CHECK(get_process(sc).Rewrite == 1 && exam_kept(sc), "0 전제: 게이트웨이 뒤 세척 시작은 이번 검사를 보존한다");
    }

    // ── A 이탈 지점 전수 × 재접촉 간격(9초 / 5분) ──
    {
        uint8_t wipeFast = 0, wipeSlow = 0, commitFail = 0, firstSlow = 0, lastSlow = 0;
        for (uint16_t n = 6; n + 1 <= total; ++n)
        {
            for (uint8_t mode = 0; mode < 2; ++mode)
            {
                as_type('W');
                touch(mgr);
                after_gateway(sc, (uint8_t)(0x50 + (n & 0x0F)), 50 + n);
                rtc_set(at(10, 0, 0));
                sc.removeAfterOps = (int16_t)n;
                touch(sc, 2, 4);
                sc.removeAfterOps = 0;
                const bool torn = get_process(sc).Rewrite != 1;
                if (mode == 0 && torn && get_ldt(sc, SECTOR3_WASHING_END).Date.Day == TRACEQ_RELEASE_DAY) ++commitFail;
                if (!torn) break;                                  // 커밋됐으면 재접촉은 종료 — 이 표본은 대상 밖
                rtc_set(mode == 0 ? at(10, 0, 9) : at(10, 5, 0));  // 사람이 다시 댄다(9초 / 5분 뒤)
                touch(sc, 2, 4);
                const bool wiped = !exam_kept(sc) && get_process(sc).Rewrite == 1;
                if (wiped)
                {
                    if (mode == 0) ++wipeFast;
                    else { ++wipeSlow; if (!firstSlow) firstSlow = (uint8_t)n; lastSlow = (uint8_t)n; }
                }
            }
        }
        tlog("  A 이탈 N=6..%u: 블록14(미리채움)까지 쓰고 커밋 전 끊김 %u개 · 9초뒤 재접촉 소거 %u · 5분뒤 재접촉 소거 %u (N=%u..%u)\n",
             total - 1, commitFail, wipeFast, wipeSlow, firstSlow, lastSlow);
        CHECK(wipeFast == 0, "A1 즉시(9초) 재접촉은 이번 검사를 지우지 않는다(13차 봉합 범위)");
        CHECK(wipeSlow == 0, "A2 slot1(4분) 넘겨 다시 대도 이번 검사를 지우지 않는다");
    }

    // ── B 같은 주기 재세척: 검사 09:00 → 세척 09:10~09:15 → 소독 09:20~09:40 → 재세척 10:00 → 덤프 ──
    {
        as_type('W');
        touch(mgr);
        after_gateway(sc, 0x61, 61);
        rtc_set(at(9, 10, 0)); touch(sc, 2, 4);                // 세척 시작
        rtc_set(at(9, 15, 0)); touch(sc, 2, 4);                // 세척 종료
        const bool keptAfterWash = exam_kept(sc);
        as_type('D');
        touch(mgr);
        rtc_set(at(9, 20, 0)); touch(sc, 2, 4);                // 소독 시작
        rtc_set(at(9, 40, 0)); touch(sc, 2, 4);                // 소독 종료
        const bool keptAfterDis = exam_kept(sc);
        as_type('W');
        touch(mgr);
        rtc_set(at(10, 0, 0)); logs_clear(); touch(sc, 2, 4);  // 재세척 시작(같은 주기 — 덤프 전)
        const bool keptAfterRewash = exam_kept(sc);
        const bool patientKept = memcmp(sc.data[SECTOR2_PATIENT_KEY], "PKEY0001", 8) == 0;
        rtc_set(at(10, 5, 0)); touch(sc, 2, 4);                // 재세척 종료
        as_type('D');
        touch(mgr);
        rtc_set(at(10, 10, 0)); touch(sc, 2, 4);
        rtc_set(at(10, 30, 0)); touch(sc, 2, 4);
        deviceOption.SetType('S');
        hard_reset(false, 2);
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc, 2, 4);
        const bool ok = serial_has("Ok!");
        const bool subjOut = serial_has("544F444159535542");   // "TODAYSUB"
        const bool pkeyOut = serial_has("504B455930303031");   // "PKEY0001"
        tlog("  B 재세척: 세척뒤=%u 소독뒤=%u 재세척뒤=%u 환자키보존=%u · 덤프 Ok=%u 환자키나감=%u 검사항목나감=%u\n",
             (unsigned)keptAfterWash, (unsigned)keptAfterDis, (unsigned)keptAfterRewash, (unsigned)patientKept,
             (unsigned)ok, (unsigned)pkeyOut, (unsigned)subjOut);
        CHECK(keptAfterWash && keptAfterDis, "B0 전제: 한 주기 동안 검사정보가 태그에 남아 있다");
        CHECK(keptAfterRewash, "B1 덤프 전 같은 주기 재세척은 이번 검사정보를 지우지 않는다");
        CHECK(ok && subjOut == pkeyOut, "B2 덤프에서 환자와 검사항목이 짝으로 나간다(환자만 나가고 검사항목은 빈 행이 아니다)");
    }

    // ── C slot1 = 0: 커밋 창에서 찢긴 뒤 9초 만에 다시 대도 지우나 ──
    {
        uint8_t wipe = 0, tried = 0;
        for (uint16_t n = total - 5; n + 1 <= total; ++n)
        {
            as_type('W');
            alarmOption.SetTimeSlot1(0);
            touch(mgr);
            after_gateway(sc, (uint8_t)(0x70 + (n & 0x0F)), 70 + n);
            rtc_set(at(10, 0, 0));
            sc.removeAfterOps = (int16_t)n;
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            if (get_process(sc).Rewrite == 1) continue;
            ++tried;
            rtc_set(at(10, 0, 9));
            touch(sc, 2, 4);
            if (!exam_kept(sc) && get_process(sc).Rewrite == 1) ++wipe;
        }
        alarmOption.SetTimeSlot1(4);
        tlog("  C slot1=0: 커밋 창 이탈 표본 %u 중 9초 뒤 재접촉이 지움 %u\n", tried, wipe);
        CHECK(wipe == 0, "C slot1=0 에서도 즉시 재접촉은 이번 검사를 지우지 않는다");
    }

    // ── D 도달 가능한 진짜 잔재: 덤프 커밋(Process{}) 뒤 소거가 실패해 어제 검사가 남았고 게이트웨이를 안 거침 ──
    {
        as_type('W');
        touch(mgr);
        after_gateway(sc, 0x7A, 90);
        set_process(sc, Process{});                             // 덤프 커밋 뒤
        set_record(sc, SECTOR1_GATEWAY, 7, yday(8, 0));         // 어제 검사(어제 세척 09:00~09:04 앞)
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ9", 8);
        rtc_set(at(10, 0, 0));
        sc.opCount = 0;
        touch(sc, 2, 4);
        const bool wiped = sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0 && get_ldt(sc, SECTOR1_GATEWAY).Date.Year == 0;
        tlog("  D 덤프 뒤 잔재: 소거=%u RW=%u 동작 %u\n", (unsigned)wiped, get_process(sc).Rewrite, sc.opCount);
        // [사장님 09-28] 완료 처리가 된 태그(공정 0)가 게이트웨이를 안 거치고 왔으면 남은 검사는 지운다.
        CHECK(wiped && get_process(sc).Rewrite == 1, "D 완료 처리 뒤 남은 어제 검사(게이트웨이 안 거침)는 세척 시작이 비운다");
    }

    // ── E 역방향: 덤프 뒤 진짜 잔재 + 커밋 창에서 찢김 + 9초 뒤 재접촉 → 잔재가 이 주기에 남나(기록만) ──
    {
        uint8_t leak = 0, tried = 0;
        for (uint16_t n = 6; n <= 46; ++n)
        {
            as_type('W');
            touch(mgr);
            after_gateway(sc, (uint8_t)(0x20 + (n & 0x0F)), 20 + n);
            set_process(sc, Process{});
            set_record(sc, SECTOR1_GATEWAY, 7, yday(8, 0));
            put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ9", 8);
            rtc_set(at(10, 0, 0));
            sc.removeAfterOps = (int16_t)n;
            touch(sc, 2, 4);
            sc.removeAfterOps = 0;
            if (get_process(sc).Rewrite == 1) continue;
            ++tried;
            rtc_set(at(10, 0, 9));
            touch(sc, 2, 4);
            if (get_process(sc).Rewrite == 1 && memcmp(sc.data[SECTOR15_EXAMINATION_SUBJECT], "OLDSUBJ9", 8) == 0) ++leak;
        }
        tlog("  E 덤프 뒤 잔재 + 이탈 N=6..46: 찢긴 표본 %u 중 재접촉 뒤에도 잔재 남음 %u (기록만)\n", tried, leak);
    }

    done();
    for (;;) {}
}
