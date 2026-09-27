// GG1 — 11차 FF ① 봉합 재추적: 'Other Machine' 거부가 **정상 흐름을 막지 않는가**(N) +
//   거부의 소리·남는 상태·회복 경로 잠금(S) + 기기번호가 저 혼자 바뀐 경우(D).
//   t_ff2m 은 ①양성대조(이동)·②③거부·④같은 기기 종료만 본다. 아래 N 은 그 밖의 종료 갈래 전수다.
#include "common.h"

static SimCard mgr, clr, a, b;
static uint8_t snap[64][16];

// ── 기기 모델(t_ff2a·t_ff2m 과 같은 방식): 옵션 EEPROM 0..177 을 통째로 갈아 끼운다 ──
static const int kEeLen = 178;
struct DevEe { uint8_t bb[kEeLen]; };
static DevEe devs[3];
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
static int dev_of(const SimCard &c, uint8_t block)
{
    int d = 0;
    memcpy(&d, c.data[block], 2);
    return d;
}
static bool at(const SimCard &c, uint8_t block, uint8_t h, uint8_t m)
{
    const LocalDateTime t = get_ldt(c, block);
    return t.Time.Hour == h && t.Time.Minute == m;
}
static void tlog_end(const char *tag, const SimCard &c, uint8_t block)
{
    const LocalDateTime t = get_ldt(c, block);
    tlog("  %s 종료기록 = 기기%d %02u:%02u · 거부=%d\n", tag, dev_of(c, block),
         t.Time.Hour, t.Time.Minute, lcd_has("Other Machine"));
}
// 세척까지 끝난 스코프(환자정보 있음 = 성공음 경로) — 세척기 번호 1
static void washed(SimCard &c, uint8_t uid, int no)
{
    char id[10], ser[10];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(c, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(c, Process{1, 0, 1, 0, 1, false, 0, 1});
    set_record(c, SECTOR2_WASHING_START, 1, rel_date(8, 0, 0));
    set_record(c, SECTOR3_WASHING_END,   1, rel_date(8, 4, 0));
}
static void server_auth()
{
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
}

int main()
{
    rtc_set(rel_date(8, 30, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    make_tag(clr, 0x12, CLEAR_TYPE_TAG, 0, "CLR", "CLR");

    // 기기 셋: D#2 · D#3 · S#9. 자동 종료 미리채움과 실제 종료를 가르려고 슬롯2 = 18분.
    deviceOption.SetType('D'); deviceOption.SetNumber(2);
    alarmOption.SetTimeSlot1(4); alarmOption.SetTimeSlot2(18); alarmOption.SetFlag(false);
    disinfectionOption.SetMaximumCount(0); disinfectionOption.SetCount(0);
    disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    disinfectionOption.SetSimultaneousDisinfectionDelay(5);
    recordOption.SetManagerDisposability(false);
    set_mgr("MGR2", "LEE");
    dev_save(0);
    deviceOption.SetNumber(3); set_mgr("MGR3", "PARK"); dev_save(1);
    deviceOption.SetType('S'); deviceOption.SetNumber(9); dev_save(2);

    // ══ N — 같은 기기에서 끝나는 정상 종료 갈래 전수(하나라도 막히면 빨강) ══

    // ── N1 동시소독(슬롯 2): host·guest 둘 다 그 기기에서 종료 ──
    {
        dev_switch(0);
        disinfectionOption.SetSimultaneousDisinfectionSlot(2);
        washed(a, 0x21, 21); washed(b, 0x22, 22);
        rtc_set(rel_date(9, 0, 0));  touch(a);            // host
        rtc_set(rel_date(9, 2, 0));  touch(b);            // guest(창 5분 안)
        rtc_set(rel_date(9, 25, 0)); logs_clear(); touch(a);
        const bool okA = !lcd_has("Other Machine") && dev_of(a, SECTOR6_DISINFECTION_END) == 2 &&
                         at(a, SECTOR6_DISINFECTION_END, 9, 25);
        tlog_end("N1 host", a, SECTOR6_DISINFECTION_END);
        rtc_set(rel_date(9, 27, 0)); logs_clear(); touch(b);
        const bool okB = !lcd_has("Other Machine") && dev_of(b, SECTOR6_DISINFECTION_END) == 2 &&
                         at(b, SECTOR6_DISINFECTION_END, 9, 27);
        tlog_end("N1 guest", b, SECTOR6_DISINFECTION_END);
        CHECK(okA, "N1 동시소독 host 의 종료가 막히지 않고 실제 시각(9:25)으로 기록된다");
        CHECK(okB, "N1 동시소독 guest 의 종료가 막히지 않고 실제 시각(9:27)으로 기록된다");
        disinfectionOption.SetSimultaneousDisinfectionSlot(1);
    }

    // ── N2 더블터치(2초 안 재접촉 = 시작 재실행) 뒤의 종료 ──
    {
        washed(a, 0x23, 23);
        rtc_set(rel_date(10, 0, 0));
        touch(a);                                          // 시작
        touch(a);                                          // 2초 안 → 시작 재실행
        const Process p = get_process(a);
        rtc_set(rel_date(10, 20, 0)); logs_clear(); touch(a);
        tlog_end("N2 더블터치 뒤", a, SECTOR6_DISINFECTION_END);
        CHECK(p.Rewrite == 2 && !lcd_has("Other Machine") &&
              dev_of(a, SECTOR6_DISINFECTION_END) == 2 && at(a, SECTOR6_DISINFECTION_END, 10, 20),
              "N2 더블터치 재시작 뒤의 종료가 막히지 않는다");
    }

    // ── N3 담당자 일회성 ON: 충전 → 시작(소모) → 종료(충전 없이) ──
    {
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                        // 일회성 충전
        washed(a, 0x24, 24);
        rtc_set(rel_date(11, 0, 0)); touch(a);             // 시작(일회성 소모)
        rtc_set(rel_date(11, 20, 0)); logs_clear(); touch(a);
        tlog_end("N3 일회성", a, SECTOR6_DISINFECTION_END);
        CHECK(!lcd_has("Other Machine") && at(a, SECTOR6_DISINFECTION_END, 11, 20),
              "N3 일회성 담당자가 소모된 뒤의 종료가 막히지 않는다");
        recordOption.SetManagerDisposability(false);
    }

    // ── N4·N5 기기번호 999(3자리)·0 에서도 같은 기기 종료가 막히지 않는다 ──
    {
        deviceOption.SetNumber(999);
        washed(a, 0x25, 25);
        rtc_set(rel_date(12, 0, 0)); touch(a);
        rtc_set(rel_date(12, 20, 0)); logs_clear(); touch(a);
        const bool ok999 = !lcd_has("Other Machine") && dev_of(a, SECTOR6_DISINFECTION_END) == 999 &&
                           at(a, SECTOR6_DISINFECTION_END, 12, 20);
        tlog_end("N4 기기999", a, SECTOR6_DISINFECTION_END);
        deviceOption.SetNumber(0);
        washed(b, 0x26, 26);
        rtc_set(rel_date(12, 30, 0)); touch(b);
        rtc_set(rel_date(12, 50, 0)); logs_clear(); touch(b);
        const bool ok0 = !lcd_has("Other Machine") && at(b, SECTOR6_DISINFECTION_END, 12, 50);
        tlog_end("N5 기기0", b, SECTOR6_DISINFECTION_END);
        CHECK(ok999, "N4 기기번호 999(3자리)에서도 같은 기기 종료가 기록된다");
        CHECK(ok0, "N5 기기번호 0 에서도 같은 기기 종료가 기록된다");
        deviceOption.SetNumber(2);
    }

    // ── N6 이동 2차 종료(섹터7·8)가 **실제 시각**으로 기록된다(자동 종료 미리채움과 가름) ──
    {
        dev_switch(0);
        washed(a, 0x27, 27);
        rtc_set(rel_date(13, 0, 0));  touch(a);            // 1차 시작 @D#2
        rtc_set(rel_date(13, 20, 0)); touch(clr);          // 액교환
        rtc_set(rel_date(13, 22, 0)); touch(a);            // 이동(1차 종료 + MV=1)
        rtc_set(rel_date(13, 50, 0));
        dev_switch(1);                                     // D#3 도착
        touch(a);                                          // 2차 시작(미리채움 = 14:08)
        rtc_set(rel_date(14, 35, 0)); logs_clear(); touch(a);
        tlog_end("N6 2차", a, SECTOR8_DISINFECTION_END);
        CHECK(!lcd_has("Other Machine") && dev_of(a, SECTOR8_DISINFECTION_END) == 3 &&
              at(a, SECTOR8_DISINFECTION_END, 14, 35),
              "N6 이동 2차 종료가 도착기(3)·실제 시각(14:35)으로 기록된다(미리채움 14:08 이 아니다)");
    }

    // ══ S — 거부의 소리·남는 상태·회복 경로 ══

    // ── S1·S2 다른 기기 종료: 거부음이고, 태그는 한 바이트도 안 바뀐다 ──
    {
        dev_switch(0);
        washed(a, 0x28, 28);
        rtc_set(rel_date(15, 0, 0)); touch(a);             // 시작 @D#2
        rtc_set(rel_date(15, 25, 0));
        dev_switch(1);                                     // D#3
        memcpy(snap, a.data, sizeof(snap));
        logs_clear(); buzz_clear();
        touch(a);                                          // ★다른 기기에서 종료 시도
        const uint8_t b60 = buzz_count(60), b600 = buzz_count(600),
                      b50 = buzz_count(50), b500 = buzz_count(500);
        const bool intact = memcmp(snap, a.data, sizeof(snap)) == 0;
        tlog("  S1 거부음: 60ms=%u 600ms=%u · 성공음 50ms=%u 알림 500ms=%u · 문구=%d · 태그무변경=%d\n",
             b60, b600, b50, b500, lcd_has("Other Machine"), intact);
        CHECK(lcd_has("Other Machine"), "S1 화면 문구");
        CHECK(b60 == 2 && b600 == 1 && b50 == 0 && b500 == 0,
              "★S1 거부음(60ms 2회 + 600ms 1회)이다 — 성공음·알림음이 아니다(사람이 귀로 안다)");
        CHECK(intact, "★S2 거부는 태그 64블록을 한 바이트도 바꾸지 않는다");
    }

    // ── S3 거부가 일회성 담당자를 소모하지 않는다 · S4 그 기기의 알람을 지우지 않는다 ──
    {
        recordOption.SetManagerDisposability(true);
        touch(mgr);                                        // D#3 에 일회성 충전
        washed(b, 0x29, 29);
        rtc_set(rel_date(16, 0, 0));
        touch(b);                                          // D#3 자기 스코프 시작 → 알람2 무장(18분)
        const int32_t rem1 = rtc.GetAlarmRemainingSeconds(2);
        touch(mgr);                                        // 다시 충전(위 시작이 소모했다)
        logs_clear();
        touch(a);                                          // a 는 D#2 에서 시작한 태그 → 거부
        const int32_t rem2 = rtc.GetAlarmRemainingSeconds(2);
        const bool rejected = lcd_has("Other Machine");
        washed(a, 0x2A, 30);                               // 새 스코프 — 일회성이 살아 있으면 시작된다
        rtc_set(rel_date(16, 5, 0)); logs_clear(); touch(a);
        const Process pa = get_process(a);
        tlog("  S3/S4 거부=%d · 알람 남은초 %ld -> %ld · 거부 뒤 새 시작 RW=%u(No Manager=%d)\n",
             rejected, (long)rem1, (long)rem2, pa.Rewrite, lcd_has("No Manager Info"));
        CHECK(rejected && rem1 > 0 && rem2 > 0 && rem1 - rem2 <= 10,
              "★S4 거부는 그 기기의 알람(자기 스코프 것)을 지우지 않는다");
        CHECK(pa.Rewrite == 2 && !lcd_has("No Manager Info"),
              "★S3 거부는 일회성 담당자를 소모하지 않는다(뒤 새 시작이 된다)");
        recordOption.SetManagerDisposability(false);
    }

    // ── S5 거부된 스코프도 서버 덤프가 되고, 소독 종료 줄은 **시작한 기기(2)** 로 나간다(회복 경로) ──
    {
        // a 는 방금 D#3 에서 시작됐으니 쓰지 않는다 — S1 의 표본을 다시 만든다.
        dev_switch(0);
        washed(b, 0x2B, 31);
        rtc_set(rel_date(17, 0, 0)); touch(b);             // 시작 @D#2 (미리채움 종료 = 17:18 · 기기2)
        rtc_set(rel_date(17, 25, 0));
        dev_switch(1); logs_clear(); touch(b);             // 거부
        const bool rej = lcd_has("Other Machine");
        dev_switch(2); server_auth(); logs_clear();
        serial_inject("Z", 1);
        touch(b);
        tlog("  S5 거부=%d · 덤프 1714(0200)=%d 1B18(0200)=%d Ok=%d\n",
             rej, serial_has("17140200"), serial_has("1B180200"), serial_has("Ok!"));
        CHECK(rej && serial_has("17140200") && serial_has("1B180200") && serial_has("Ok!"),
              "★S5 거부된 스코프도 덤프되고 소독 시작·종료 줄이 시작 기기(2)로 나간다(막힘이 영구가 아니다)");
    }

    // ── S6 `!mMovable` 가드의 뜻: 액교환한 기기는 **다른 기기에서 시작한 스코프도 이동으로 받는다** ──
    //   이것이 'Other Machine' 거부 뒤 사람이 쓸 수 있는 회복 경로다(그 스코프의 2차 소독이 기록된다).
    {
        dev_switch(0);
        washed(b, 0x2D, 33);
        rtc_set(rel_date(19, 0, 0)); touch(b);             // 1차 시작 @D#2
        rtc_set(rel_date(19, 20, 0));
        dev_switch(1);                                     // D#3 (이동 표시 없음)
        logs_clear(); touch(b);                            // 양성대조 — 거부되어야 한다
        const bool rej0 = lcd_has("Other Machine");
        touch(clr);                                        // ★D#3 에서 액교환 → 이동 표시
        rtc_set(rel_date(19, 25, 0)); logs_clear(); touch(b);
        const Process pm = get_process(b);
        tlog("  S6 액교환 전 거부=%d → 액교환 뒤: MV=%u RW=%u 거부=%d (1차종료기기=%d)\n",
             rej0, (unsigned)pm.MovementNeeded, pm.Rewrite, lcd_has("Other Machine"),
             dev_of(b, SECTOR6_DISINFECTION_END));
        CHECK(rej0 && !lcd_has("Other Machine") && pm.MovementNeeded && pm.Rewrite == 0,
              "★S6 액교환한 기기는 다른 기기에서 시작한 스코프도 이동으로 받는다(!mMovable 가드 = 거부 뒤 회복 경로)");
    }

    // ══ D — 기기번호가 저 혼자 바뀐 경우(EEPROM 찢김 · 10차 판정 실측 300→44) ══
    {
        dev_switch(0);
        washed(a, 0x2C, 32);
        rtc_set(rel_date(18, 0, 0)); touch(a);             // 시작 @D#2
        deviceOption.SetNumber(44);                        // 낮은 바이트만 써진 모양
        rtc_set(rel_date(18, 25, 0)); logs_clear(); run_loops(2);
        const bool shown = lcd_has("D:44");
        touch(a);
        const bool blocked = lcd_has("Other Machine");
        deviceOption.SetNumber(2);                         // 사람이 번호를 되돌린다
        rtc_set(rel_date(18, 30, 0)); logs_clear(); touch(a);
        tlog("  D1 번호 44 로 바뀜: 화면표시=%d 종료막힘=%d → 번호 복구 뒤 %02u:%02u 거부=%d\n",
             shown, blocked, get_ldt(a, SECTOR6_DISINFECTION_END).Time.Hour,
             get_ldt(a, SECTOR6_DISINFECTION_END).Time.Minute, lcd_has("Other Machine"));
        CHECK(shown, "★D1 기기번호가 저 혼자 바뀌면 홈 화면에 그 번호가 보인다(사람이 진단할 수 있다)");
        CHECK(!lcd_has("Other Machine") && at(a, SECTOR6_DISINFECTION_END, 18, 30),
              "★D1 번호를 되돌리면 그 스코프의 종료가 정상 기록된다(회복 경로가 있다)");
    }

    done();
    for (;;) {}
}
