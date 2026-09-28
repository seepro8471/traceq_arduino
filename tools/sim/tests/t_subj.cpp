// 사장님 지시(09-28) — 지난 검사의 게이트웨이 잔재(블록5 검사일시 · 섹터15 검사항목)를 세척 시작에서 비운다.
//  이 둘을 지우는 것은 서버 덤프와 다음 게이트웨이 접촉뿐이라, 서버가 없고 게이트웨이를 안 쓰는 현장에서는
//  **어제 검사항목이 오늘 대장에 실렸다**(12차 GG1 P3-5).
//  ★판정은 **표지만**(사장님 선택 1): 공정 전부 0 + Status 0 + 검사일시 있음 → 지운다 · 시간 비교 없음.
//   환자와 함께 쓴 이번 검사(Status 1)는 시각과 무관하게 남는다 — 자정을 넘긴 검사(③)도 그래서 보존된다.
//   (12~14차의 시간·관계 판정은 2.2.32 에서 걷어냈다.)
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

// 지난 주기가 남은 스코프 + 검사 정보(검사일시·검사항목)를 원하는 시각으로 심는다
static void scope_with_exam(SimCard &c, uint8_t uid, int no, const DateTime &lastWash,
                            const DateTime &exam, const char *subj)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{0, 0, 0, 0, 0, false, 0, 0});      // 공정 전부 0 · Status 0 = 덤프(완료 처리) 뒤
    set_record(c, SECTOR2_WASHING_START, 1, lastWash);
    set_record(c, SECTOR3_WASHING_END, 1, lastWash);
    set_record(c, SECTOR1_GATEWAY, 7, exam);                  // 블록5 = 본체번호 + 검사일시
    put_block(c, SECTOR15_EXAMINATION_SUBJECT, subj, (uint8_t)strlen(subj));
}

static bool subj_is(const SimCard &c, const char *s)
{
    return memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT], s, strlen(s)) == 0;
}
static bool subj_empty(const SimCard &c)
{
    return c.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── ① 지난 주기 잔재: 완료 뒤 Status 0 + 검사일시 있음(어제 08:00) → 비운다 ──
    {
        as_type('W');
        touch(mgr);
        scope_with_exam(sc, 0x31, 31, yday(9, 0), yday(8, 0), "OLDSUBJ1");
        logs_clear();
        touch(sc, 2, 4);                                      // 오늘 세척 시작
        const LocalDateTime gw = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  ① 잔재: 검사항목비움=%u 검사일시연도=%u\n", (unsigned)subj_empty(sc), gw.Date.Year);
        CHECK(subj_empty(sc) && gw.Date.Year == 0,
              "① 지난 주기 검사 잔재(검사일시·검사항목)는 세척 시작이 비운다");
    }

    // ── ② 이번 검사: 완료 뒤 게이트웨이가 환자와 함께 쓴 검사(Status 1 · 오늘 08:00) → 보존 ──
    {
        as_type('W');
        touch(mgr);
        scope_with_exam(sc, 0x32, 32, yday(9, 0), rel_date(8, 0, 0), "NEWSUBJ1");
        set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});   // 환자 있음(Status 1) — 완료 뒤 게이트웨이가 새로 쓴 검사
        logs_clear();
        touch(sc, 2, 4);
        const LocalDateTime gw = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  ② 이번 검사: 항목보존=%u 검사시각=%02u:%02u\n", (unsigned)subj_is(sc, "NEWSUBJ1"),
             gw.Time.Hour, gw.Time.Minute);
        CHECK(subj_is(sc, "NEWSUBJ1") && gw.Time.Hour == 8,
              "② 완료 뒤 게이트웨이가 환자와 함께 쓴 이번 검사(Status 1)는 보존된다(표지만 판정)");
    }

    // ── ③ ★자정 넘김: 검사(어제 23:50 · Status 1) → 00:10 세척 → 보존 ──
    //    검사는 어제 날짜이지만 이번 검사다 — "오늘인가" 같은 날짜 판정을 되살리면 깨질 자리.
    {
        as_type('W');
        touch(mgr);
        rtc_set(DateTime(rel_date(0, 10, 0).unixtime()));     // 오늘 00:10 에 세척
        scope_with_exam(sc, 0x33, 33, yday(9, 0), yday(23, 50), "NIGHTSUB");
        set_process(sc, Process{1, 0, 0, 0, 0, false, 0, 0});   // 환자 있음(Status 1)
        logs_clear();
        touch(sc, 2, 4);
        const LocalDateTime gw = get_ldt(sc, SECTOR1_GATEWAY);
        tlog("  ③ 자정 넘김: 항목보존=%u 검사시각=%02u:%02u(어제)\n", (unsigned)subj_is(sc, "NIGHTSUB"),
             gw.Time.Hour, gw.Time.Minute);
        CHECK(subj_is(sc, "NIGHTSUB") && gw.Time.Hour == 23,
              "★③ 자정을 넘긴 검사(23:50 → 00:10 세척 · Status 1)도 보존된다 — 표지로 판정하니 시각은 무관");
        rtc_set(rel_date(10, 0, 0));
    }

    // ── ④ 블록5 를 못 읽으면 손대지 않는다(짐작하지 않는다) ──
    {
        as_type('W');
        touch(mgr);
        scope_with_exam(sc, 0x34, 34, yday(9, 0), yday(8, 0), "OLDSUBJ2");
        sc.readErrBlock = SECTOR1_GATEWAY;
        sc.readErrTimes = 4;
        logs_clear();
        touch(sc, 2, 4);
        sc.readErrBlock = -1;
        sc.readErrTimes = 0;
        tlog("  ④ 읽기 실패: 항목그대로=%u\n", (unsigned)subj_is(sc, "OLDSUBJ2"));
        CHECK(subj_is(sc, "OLDSUBJ2"), "④ 검사일시를 못 읽으면 아무것도 지우지 않는다");
    }

    // ── ⑤ 종단: 잔재 태그가 한 주기를 돌아도 옛 검사항목이 덤프로 나가지 않는다 ──
    //     OLDSUBJ1 = 4F 4C 44 53 55 42 4A 31
    {
        as_type('W');
        touch(mgr);
        scope_with_exam(sc, 0x35, 35, yday(9, 0), yday(8, 0), "OLDSUBJ1");
        touch(sc, 2, 4);                                      // 세척 시작(여기서 비워진다)
        sim_advance_ms(20UL * 60 * 1000);
        touch(sc, 2, 4);                                      // 세척 종료
        as_type('D');
        touch(sc, 2, 4);                                      // 소독 시작
        sim_advance_ms(20UL * 60 * 1000);
        touch(sc, 2, 4);                                      // 소독 종료
        deviceOption.SetType('S');
        hard_reset(false, 2);
        serial_inject("Z", 1);
        GUARDED(serialEvent());
        run_loops(1);
        logs_clear();
        serial_inject("Z", 1);
        touch(sc, 2, 4);
        const bool ok = serial_has("Ok!");
        const bool leaked = serial_has("4F4C445355424A31");   // "OLDSUBJ1"
        tlog("  ⑤ 종단: Ok!=%u 옛검사항목 전송=%u\n", (unsigned)ok, (unsigned)leaked);
        CHECK(ok, "⑤ 전제: 한 주기 뒤 덤프가 Ok! 로 끝난다");
        CHECK(!leaked, "★⑤ 옛 검사항목(OLDSUBJ1)이 덤프로 나가지 않는다");
    }

    done();
    for (;;) {}
}
