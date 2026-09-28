// 14회차 II-B 잠금 2 — '지난 주기 소독 종료를 안 댄' 흐름(미리채움)에서 덤프 뒤 **폴백(환자 없음)** 으로 받은
//  이번 검사일시는 세척 시작이 지운다(사장님 선택 1 의 대가 · 표지만 판정이라 블록24 미리채움·시각은 안 본다).
//  F. 지난 주기 소독 종료를 안 댔다(자동 종료 미리채움 = 소독 시작 + slot2). 그 미리채움보다 **앞서**
//     덤프 → 게이트웨이 폴백(이번 검사 · Status 0) → 세척 시작이 오면 이번 검사일시가 어떻게 되나.
//     실제 흐름으로 만든다: 소독기(slot2=30) 시작 10:00 → 서버 덤프 10:22 → 게이트웨이 폴백 10:25 → 세척 10:45.
//  (같은 주기 재세척 보존은 `t_ii1` B 가 잠근다.)
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};

static DateTime at(uint8_t h, uint8_t m, uint8_t s) { return rel_date(h, m, s); }

static void as_type(char t)
{
    deviceOption.SetType(t);
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
}
static void as_server()
{
    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}

int main()
{
    rtc_set(at(9, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── F ──
    {
        make_tag(sc, 0x31, SCOPE_TYPE_TAG, 31, "SC0031", "S0031");
        as_type('W');
        touch(mgr);
        rtc_set(at(9, 40, 0)); touch(sc, 2, 4);                 // 지난 주기 세척 시작(자동 종료 09:44)
        as_type('D');
        alarmOption.SetTimeSlot2(30);                            // 소독 30분 설정(실제 기계는 더 짧게 끝남)
        touch(mgr);
        rtc_set(at(10, 0, 0)); touch(sc, 2, 4);                  // 소독 시작 — 블록24 = 10:30:00 미리채움
        const LocalDateTime pre24 = get_ldt(sc, SECTOR6_DISINFECTION_END);
        as_server();
        rtc_set(at(10, 22, 0));
        logs_clear();
        serial_inject("Z", 1);
        touch(sc, 2, 4);                                         // 덤프(소독 종료는 안 댐)
        const bool dumped = serial_has("Ok!") && get_process(sc).WashingStatus == 0;
        as_type('G');
        rtc_set(at(10, 25, 0));
        touch(sc, 2, 4);                                         // 게이트웨이 폴백 — 이번 검사 10:25
        const LocalDateTime ex = get_ldt(sc, SECTOR1_GATEWAY);
        const bool exam0 = ex.Date.Year != 0 && sc.data[SECTOR15_EXAMINATION_SUBJECT][0] != 0;
        as_type('W');
        touch(mgr);
        rtc_set(at(10, 45, 0)); logs_clear(); touch(sc, 2, 4);  // 이번 주기 세척 시작
        const LocalDateTime ex2 = get_ldt(sc, SECTOR1_GATEWAY);
        const bool kept = ex2.Date.Year != 0 && sc.data[SECTOR15_EXAMINATION_SUBJECT][0] != 0;
        tlog("  F 블록24(미리채움)=%02u:%02u · 덤프=%u · 검사 %02u:%02u 기록=%u · 세척 시작 뒤 보존=%u RW=%u\n",
             pre24.Time.Hour, pre24.Time.Minute, (unsigned)dumped, ex.Time.Hour, ex.Time.Minute,
             (unsigned)exam0, (unsigned)kept, get_process(sc).Rewrite);
        CHECK(dumped && exam0, "F0 전제: 덤프 뒤 게이트웨이가 이번 검사를 기록했다");
        // [사장님 선택 1 · 09-28] 표지만 판정: 완료 뒤 환자 없이(폴백) 받은 검사일시는 Status 0 이라 세척 시작이 지운다 —
        //  잃는 것은 검사 시각 하나(환자 정보는 원래 없다). 이 CHECK 는 그 대가를 잠근다(바꾸면 판정과 함께 뒤집는다).
        CHECK(!kept, "F 완료 뒤 폴백(환자 없음)으로 받은 검사일시는 세척 시작이 지운다 — 선택 1 의 대가");
        alarmOption.SetTimeSlot2(18);
    }

    done();
    for (;;) {}
}
