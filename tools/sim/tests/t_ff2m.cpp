// 11차 FF2 P1-1 봉합 잠금 — **종료는 시작한 기기에서만**.
//  이동 표시(mMovable)는 RAM 이라 ① 그 소독기 재부팅 ② 다른 태그 한 번 거부로 사라진다. 사라진 뒤 도착
//  소독기에 대면 종전엔 또 '종료' 로 처리돼 앞 기기의 종료 시각을 **조용히 덮었다**(도착기 소독이 대장에서
//  사라져 소독 횟수 과소 = 액교환이 늦어진다). 사장님 확인(09-27): 액교환 없는 기기 간 이동도, 운용 중
//  기기번호 변경도 없다 → 다른 기기의 종료는 거부한다.
#include "common.h"

static SimCard mgr, sc, clr, other;

// ── 기기 모델: 옵션 EEPROM 0..177 을 통째로 갈아 끼운다(t_ff2a 와 같은 방식) ──
static const int kEeLen = 178;
struct DevEe { uint8_t b[kEeLen]; };
static DevEe devs[3];
static void dev_save(uint8_t i) { for (int a = 0; a < kEeLen; ++a) devs[i].b[a] = EEPROM.read(a); }
static void dev_switch(uint8_t i)
{
    card_remove();
    for (int a = 0; a < kEeLen; ++a) EEPROM.update(a, devs[i].b[a]);
    hard_reset(false, 2);          // 그 기기의 RAM(host/guest·이동 플래그·알람)은 비어 있다
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
    DisinfectionRecord r{};
    memcpy(&r, c.data[block], sizeof(r));
    return r.MachineNumber;
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

int main()
{
    rtc_set(rel_date(13, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    make_tag(clr, 0x12, CLEAR_TYPE_TAG, 0, "CLR", "CLR");
    make_tag(other, 0x13, SCOPE_TYPE_TAG, 99, "SC0099", "S0099");   // 세척 안 된 스코프(거부용)
    set_process(other, Process{0, 0, 0, 0, 0, false, 0, 0});

    // 소독기 둘 만들기 — D#2(출발) · D#3(도착)
    deviceOption.SetType('D');
    deviceOption.SetNumber(2);
    set_mgr("MGR2", "LEE");
    dev_save(0);
    deviceOption.SetNumber(3);
    set_mgr("MGR3", "PARK");
    dev_save(1);

    // ── ① 양성대조: 정상 이동(액교환 → 이동 → 도착기 2차)이 그대로 동작한다 ──
    {
        dev_switch(0);                                 // D#2
        washed(sc, 0x21, 21);
        touch(sc);                                     // 1차 소독 시작
        rtc_set(rel_date(13, 20, 0));
        touch(clr);                                    // 액교환 — 이동 표시가 선다
        rtc_set(rel_date(13, 22, 0));
        touch(sc);                                     // 이동 처리(1차 종료 + MV=1)
        const Process p = get_process(sc);
        const int e1 = dev_of(sc, SECTOR6_DISINFECTION_END);
        rtc_set(rel_date(13, 30, 0));
        dev_switch(1);                                 // D#3 도착
        logs_clear();
        touch(sc);                                     // 2차 시작 — 막히지 않아야 한다
        const int s2 = dev_of(sc, SECTOR7_DISINFECTION_START);
        tlog("  ① 이동: MV=%u RW=%u 1차종료기기=%d 2차시작기기=%d 거부=%d\n",
             (unsigned)p.MovementNeeded, p.Rewrite, e1, s2, lcd_has("Other Machine"));
        CHECK(p.MovementNeeded && p.Rewrite == 0 && e1 == 2, "① 양성대조: 이동이 1차 종료를 출발기(2)로 커밋한다");
        CHECK(s2 == 3 && !lcd_has("Other Machine"), "① 양성대조: 도착기(3) 2차 시작은 막히지 않는다");
    }

    // ── ② 잠금: 액교환 뒤 **그 소독기가 재부팅**되면 이동 표시가 사라진다 → 도착기 종료는 거부 ──
    {
        rtc_set(rel_date(14, 0, 0));
        dev_switch(0);                                 // D#2
        washed(sc, 0x22, 22);
        touch(sc);                                     // 1차 소독 시작(기기 2)
        rtc_set(rel_date(14, 20, 0));
        touch(clr);                                    // 액교환 — 표시 섬
        dev_switch(0);                                 // ★재부팅(같은 기기) — RAM 표시 사라짐
        rtc_set(rel_date(14, 22, 0));
        touch(sc);                                     // 출발기 종료(정상 기록 — 같은 기기)
        const int e1 = dev_of(sc, SECTOR6_DISINFECTION_END);
        const LocalDateTime t1 = get_ldt(sc, SECTOR6_DISINFECTION_END);
        rtc_set(rel_date(14, 50, 0));
        dev_switch(1);                                 // D#3 도착
        logs_clear();
        touch(sc);                                     // ★도착기 종료 시도
        const int e2 = dev_of(sc, SECTOR6_DISINFECTION_END);
        const LocalDateTime t2 = get_ldt(sc, SECTOR6_DISINFECTION_END);
        tlog("  ② 재부팅 뒤: 출발기 종료 기기=%d %02u:%02u → 도착기 접촉 뒤 기기=%d %02u:%02u 거부=%d\n",
             e1, t1.Time.Hour, t1.Time.Minute, e2, t2.Time.Hour, t2.Time.Minute,
             lcd_has("Other Machine"));
        CHECK(lcd_has("Other Machine"), "★② 다른 기기에서의 종료는 거부음으로 알린다(조용한 덮어쓰기 금지)");
        CHECK(e2 == e1 && t2.Time.Hour == t1.Time.Hour && t2.Time.Minute == t1.Time.Minute,
              "★② 앞 기기의 종료 기록(기기번호·시각)이 덮이지 않는다");
    }

    // ── ③ 잠금: 액교환 뒤 **다른 태그가 한 번 거부**되면(재부팅 없이) 표시가 내려간다 ──
    {
        rtc_set(rel_date(15, 0, 0));
        dev_switch(0);
        washed(sc, 0x23, 23);
        touch(sc);                                     // 1차 시작(기기 2)
        rtc_set(rel_date(15, 20, 0));
        touch(clr);                                    // 액교환
        touch(other);                                  // ★세척 안 된 스코프 거부 → 이동 표시 내려감
        rtc_set(rel_date(15, 22, 0));
        touch(sc);                                     // 출발기 종료
        const LocalDateTime t1 = get_ldt(sc, SECTOR6_DISINFECTION_END);
        rtc_set(rel_date(15, 50, 0));
        dev_switch(1);
        logs_clear();
        touch(sc);
        const LocalDateTime t2 = get_ldt(sc, SECTOR6_DISINFECTION_END);
        tlog("  ③ 거부로 표시 내려간 뒤: %02u:%02u → %02u:%02u 거부=%d\n",
             t1.Time.Hour, t1.Time.Minute, t2.Time.Hour, t2.Time.Minute, lcd_has("Other Machine"));
        CHECK(lcd_has("Other Machine") && t2.Time.Minute == t1.Time.Minute,
              "★③ 거부로 이동 표시가 내려간 경우도 같다(재부팅이 없어도 된다)");
    }

    // ── ④ 대조: 같은 기기에서의 정상 종료는 막히지 않는다(막힘이 생기면 안 된다) ──
    {
        rtc_set(rel_date(16, 0, 0));
        dev_switch(0);
        washed(sc, 0x24, 24);
        touch(sc);                                     // 시작(기기 2)
        rtc_set(rel_date(16, 25, 0));
        logs_clear();
        touch(sc);                                     // 같은 기기 종료
        const LocalDateTime e = get_ldt(sc, SECTOR6_DISINFECTION_END);
        tlog("  ④ 대조 같은 기기 종료: %02u:%02u 거부=%d\n", e.Time.Hour, e.Time.Minute,
             lcd_has("Other Machine"));
        CHECK(!lcd_has("Other Machine") && e.Time.Hour == 16 && e.Time.Minute == 25,
              "④ 대조: 같은 기기에서의 종료는 그대로 기록된다");
    }

    done();
    for (;;) {}
}
