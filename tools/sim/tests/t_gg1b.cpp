// GG1 — 11차 FF ② 봉합 재추적: `prevStatus == 0 || prevStatus > 3` 의 **여집합 전수**와
//   그 갈래가 비우는 블록의 범위. Status=2 갈래는 블록 5·8·9·섹터15 를 비우고(update_process),
//   Status 0 은 완료 뒤 검사가 있으면 표지 판정이 블록 5·섹터15 까지(사장님 선택 1), >3 갈래는 **블록 8·9 만** 비운다.
//   블록 5·섹터15 가 남는 >3 표본(F)과 게이트웨이 접촉(G)으로 잔재가 어디서 걷히는지 잰다.
#include "common.h"

static SimCard sc, mgr;
static const int kEeLen = 178;
struct DevEe { uint8_t bb[kEeLen]; };
static DevEe devs[4];   // 0=W#1 1=D#2 2=S#9 3=G#7
static void dev_save(uint8_t i) { for (int x = 0; x < kEeLen; ++x) devs[i].bb[x] = EEPROM.read(x); }
static void dev_switch(uint8_t i)
{
    card_remove();
    for (int x = 0; x < kEeLen; ++x) EEPROM.update(x, devs[i].bb[x]);
    hard_reset(false, 2);
}
static void set_mgr(const char *key, const char *name)
{
    unsigned char k[ManagerOption::KEY_SIZE]{}, n[ManagerOption::NAME_SIZE]{};
    strncpy((char *)k, key, sizeof(k));
    strncpy((char *)n, name, sizeof(n));
    managerOption.SetData(k, n);
}
static void server_auth()
{
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}
static bool blk_is(const SimCard &c, uint8_t b, const char *s)
{
    return strncmp((const char *)c.data[b], s, strlen(s)) == 0;
}
static bool blk_zero(const SimCard &c, uint8_t b)
{
    for (uint8_t i = 0; i < 16; ++i) if (c.data[b][i]) return false;
    return true;
}
static int gw_num(const SimCard &c)
{
    Gateway g{};
    memcpy(&g, c.data[SECTOR1_GATEWAY], sizeof(g));
    return g.Number;
}

// "덤프까지 끝났다고 보이는" 스코프인데 **소거가 반만 된** 태그:
//  블록 5(게이트웨이 본체번호 7777 + 지난 검사일시)·8·9(지난 환자)·섹터15(지난 검사항목) 가 남아 있다.
static void stale(SimCard &c, uint8_t uid, int no, uint8_t status)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{status, 0, 0, 0, 0, false, 0, 0});
    Gateway g{7777, DefaultRtc::ToLocalDateTime(rel_date(7, 7, 7))};
    put_block(c, SECTOR1_GATEWAY, &g, sizeof(g));
    put_block(c, SECTOR2_PATIENT_KEY,  "OLDKEY99", 8);
    put_block(c, SECTOR2_PATIENT_NAME, "OLDNAME9", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT,  "OLDSUBJ1", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT2, "OLDSUBJ2", 8);
    put_block(c, SECTOR15_EXAMINATION_SUBJECT3, "OLDSUBJ3", 8);
}

struct Res { bool pk, pn, gw, s15; uint16_t ops; };
// 세척 시작 한 번만 — 어느 블록이 비워지나
static Res wash_start_only(uint8_t uid, int no, uint8_t status)
{
    dev_switch(0);
    stale(sc, uid, no, status);
    rtc_set(rel_date(9, 0, 0));
    const uint16_t o0 = sc.opCount;
    touch(sc);
    Res r{};
    r.pk  = blk_zero(sc, SECTOR2_PATIENT_KEY);
    r.pn  = blk_zero(sc, SECTOR2_PATIENT_NAME);
    r.gw  = blk_zero(sc, SECTOR1_GATEWAY);
    r.s15 = blk_zero(sc, SECTOR15_EXAMINATION_SUBJECT);
    r.ops = sc.opCount - o0;
    tlog("  Status=%3u → 커밋 RW=%u Status=%u · 비워짐 키=%d 이름=%d 게이트웨이=%d 검사항목=%d · 동작 %u\n",
         status, get_process(sc).Rewrite, get_process(sc).Status,
         r.pk, r.pn, r.gw, r.s15, r.ops);
    return r;
}

int main()
{
    rtc_set(rel_date(8, 30, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    alarmOption.SetTimeSlot1(4); alarmOption.SetTimeSlot2(18); alarmOption.SetFlag(false);
    recordOption.SetManagerDisposability(false); recordOption.SetPatientCheck(false);
    deviceOption.SetType('W'); deviceOption.SetNumber(1); set_mgr("W1KEY", "WON"); dev_save(0);
    deviceOption.SetType('D'); deviceOption.SetNumber(2); set_mgr("D2KEY", "DOO");
    disinfectionOption.SetMaximumCount(0); disinfectionOption.SetCount(0);
    disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    dev_save(1);
    deviceOption.SetType('S'); deviceOption.SetNumber(9); dev_save(2);
    deviceOption.SetType('G'); deviceOption.SetNumber(7); dev_save(3);

    // ── E: Status 여집합 전수 — 어느 갈래가 어느 블록을 비우나 ──
    Res r[6];
    const uint8_t sv[6] = {0, 1, 2, 3, 4, 200};
    for (uint8_t i = 0; i < 6; ++i) r[i] = wash_start_only(0x30 + i, 40 + i, sv[i]);

    CHECK(r[0].ops > 0 && r[5].ops > 0, "E0 양성대조: 모든 Status 표본에서 세척 시작이 실제로 돌았다");
    CHECK(r[0].pk && r[0].pn && r[2].pk && r[2].pn && r[4].pk && r[4].pn && r[5].pk && r[5].pn,
          "E1 Status 0·2·4·200 은 옛 환자 블록(8·9)이 비워진다(FF ② 봉합 포함)");
    CHECK(!r[1].pk && !r[3].pk,
          "E2 Status 1·3(환자정보 있음)은 옛 환자 블록이 보존된다");
    CHECK(r[2].gw && r[2].s15,
          "E3 Status=2 레거시 갈래는 블록 5(게이트웨이)·섹터15(검사항목)까지 비운다");
    tlog("  E4 0·>3 갈래가 블록 5 를 비우나 = %d/%d · 섹터15 를 비우나 = %d/%d (Status=2 갈래는 %d·%d)\n",
         r[0].gw, r[5].gw, r[0].s15, r[5].s15, r[2].gw, r[2].s15);
    // 15차 사장님 A6: 표지 판정(공정 0 + 검사 있음)은 Status 0 과 **모르는 값(>3)** 둘 다 — 손상 Status 태그가 환자 없는 행에 옛 검사를 싣지 않게.
    CHECK(r[0].gw && r[0].s15 && r[5].gw && r[5].s15,
          "E4(15차 A6) Status 0 과 200(>3) 둘 다 표지 판정으로 블록5·섹터15 를 비운다");
    CHECK(!r[1].gw && !r[1].s15 && !r[3].gw && !r[3].s15,
          "E4b Status 1·3(환자 있음)은 검사를 남긴다 — 경계는 >3 이지 >=3 이 아니다(16차 IV-H)");
    // Status==2 는 **1.0 에서 온 태그의 전환 1회**다(2.0 은 Status 에 0·1 만 쓴다) — 레거시 갈래가 블록 5·8·9 와
    //  섹터15 3블록을 비우므로 더 무겁다 → 매 주기 도는 갈래와 **따로** 잠근다(상한 하나를 올려 덮지 않는다).
    // 14차(사장님 선택 1 · 표지만 판정): Status 0(완료 뒤 · 환자 없음 · 검사 있음)도 소거 갈래다 — 태그마다 1회.
    //  매 주기 갈래 = Status 1·3(환자 있음 → 남긴다) · 4·200(모르는 값 → 8·9 만 비움). 소거 갈래 = Status 0(표지 판정) · 2(레거시).
    uint16_t mx = 0, mxClear = 0;
    for (uint8_t i = 0; i < 6; ++i)
    {
        if (sv[i] == 0 || sv[i] == 2 || sv[i] > 3) { if (r[i].ops > mxClear) mxClear = r[i].ops; }
        else                                        { if (r[i].ops > mx)      mx      = r[i].ops; }
    }
    tlog("  E 접촉 예산: 매 주기 갈래(1·3) 최대 %u(≤40) · 소거 갈래(0·2·>3 · 1회) 최대 %u(≤45)\n",
         (unsigned)mx, (unsigned)mxClear);
    CHECK(mx <= 40, "E5 매 주기 도는 Status 갈래(1·3)는 접촉 예산 40 안");
    CHECK(mxClear > mx && mxClear <= 45,
          "E5b 소거 갈래(Status 0·>3 표지 판정 · Status 2 레거시)는 45 안 — 태그마다 1회라 무거워도 된다");

    // ── F: 그 잔재가 실제로 PC 로 나가는가(Status=200 표본 한 주기 + 덤프) ──
    {
        dev_switch(0);
        stale(sc, 0x3A, 50, 200);
        rtc_set(rel_date(10, 0, 0));  touch(sc);            // 세척 시작
        rtc_set(rel_date(10, 20, 0)); touch(sc);            // 세척 종료
        dev_switch(1);
        rtc_set(rel_date(10, 30, 0)); touch(sc);            // 소독 시작
        rtc_set(rel_date(10, 50, 0)); touch(sc);            // 소독 종료
        dev_switch(2); server_auth(); logs_clear();
        serial_inject("Z", 1);
        touch(sc);
        const bool subj = serial_has("4F4C445355424A31");   // "OLDSUBJ1"
        const bool key  = serial_has("4F4C444B45593939");   // "OLDKEY99"
        tlog("  F 덤프: 옛 검사항목=%d 옛 등록번호=%d 게이트웨이번호=%d Ok=%d\n",
             subj, key, gw_num(sc), serial_has("Ok!"));
        CHECK(serial_has("Ok!"), "F0 양성대조: 덤프가 Ok! 로 끝났다");
        CHECK(!key, "F1 옛 등록번호는 덤프에 나가지 않는다(FF ② 봉합이 듣는다)");
        tlog("  F★ 옛 검사항목이 덤프에 나갔나 = %d (Status 200 도 15차 A6 로 표지 판정 안 — 완료 뒤 검사 있음이면 세척 시작이 블록 5·섹터15 를 비운다 · 17차 라벨 정정)\n", subj);
    }

    // ── G: 게이트웨이 접촉(폴백)도 블록 5·섹터15 를 새로 쓴다 — 게이트웨이는 Status 를 가리지 않는다(완료만 본다) ──
    {
        dev_switch(3);                                       // G#7
        stale(sc, 0x3B, 51, 0);
        rtc_set(rel_date(11, 0, 0));
        logs_clear();
        touch(sc);                                           // 환자정보 없는 게이트웨이 접촉(폴백)
        // ★판정식 정정: 폴백 갈래는 섹터15 를 소거한 **뒤 블록 60 에 검사일시 문자열**을 쓴다
        //  (write_no_patient_info) — 그래서 60 은 0 이 아니고 "옛 검사항목이 아닌 것" 이 기준이다.
        tlog("  G 게이트웨이 뒤: 번호=%d 블록60='%.15s' 옛검사항목남음=%d 61비움=%d 62비움=%d 환자키비움=%d\n",
             gw_num(sc), (const char *)sc.data[SECTOR15_EXAMINATION_SUBJECT],
             blk_is(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ1"),
             blk_zero(sc, SECTOR15_EXAMINATION_SUBJECT2), blk_zero(sc, SECTOR15_EXAMINATION_SUBJECT3),
             blk_zero(sc, SECTOR2_PATIENT_KEY));
        CHECK(gw_num(sc) == 7 && !blk_is(sc, SECTOR15_EXAMINATION_SUBJECT, "OLDSUBJ1") &&
              blk_zero(sc, SECTOR15_EXAMINATION_SUBJECT2) && blk_zero(sc, SECTOR15_EXAMINATION_SUBJECT3),
              "G1 게이트웨이 접촉이 블록 5 를 자기 번호로 쓰고 섹터15(옛 검사항목)를 지운다 — 세척 시작 표지 판정(Status 0 만) 밖 잔재도 걷는 자리");
    }

    done();
    for (;;) {}
}
