// 한 접촉의 카드 동작(인증·읽기·쓰기) 수 = **접촉 예산**.
// 사장님 확인(09-27): 현장에서 태그는 올려 두지 않는다 — 접촉하고 바로 뗀다(1초 미만). 그 사이에 읽기·쓰기가
// 다 끝나야 한다. **회당 실시간은 이 장치로 모른다**(전에 쓴 '3.3ms' 는 EEPROM 바이트 쓰기 값이었다 · 9차 DD1)
// — 그래서 상한은 **동작 수**로만 박는다. 동작이 크게 늘면 빠른 접촉에서 커밋 전에 끊기기 시작한다.
#include "common.h"

static SimCard mgr, sc;

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    mgr.opCount = 0;
    touch(mgr);
    const uint16_t opMgr = mgr.opCount;

    make_tag(sc, 0x21, SCOPE_TYPE_TAG, 21, "SC0021", "S0021");
    set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
    sc.opCount = 0;
    touch(sc);
    const uint16_t opWashStart = sc.opCount;

    sim_advance_ms(20UL * 60 * 1000);
    sc.opCount = 0;
    touch(sc);
    const uint16_t opWashEnd = sc.opCount;

    deviceOption.SetType('D');
    hard_reset(false, 2);
    touch(mgr);
    make_tag(sc, 0x22, SCOPE_TYPE_TAG, 22, "SC0022", "S0022");
    set_process(sc, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    sc.opCount = 0;
    touch(sc);
    const uint16_t opDisStart = sc.opCount;

    sim_advance_ms(20UL * 60 * 1000);
    sc.opCount = 0;
    touch(sc);
    const uint16_t opDisEnd = sc.opCount;

    deviceOption.SetType('S');
    hard_reset(false, 2);
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    make_tag(sc, 0x23, SCOPE_TYPE_TAG, 23, "SC0023", "S0023");
    set_process(sc, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(sc, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(sc, SECTOR3_WASHING_END, 1, rel_date(9, 4, 0));
    set_record(sc, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(sc, SECTOR6_DISINFECTION_END, 2, rel_date(9, 23, 0));
    serial_inject("Z", 1);
    sc.opCount = 0;
    touch(sc);
    const uint16_t opDump = sc.opCount;

    tlog("  접촉당 카드 동작 — 담당자 %u · 세척 시작 %u · 세척 종료 %u\n",
         (unsigned)opMgr, (unsigned)opWashStart, (unsigned)opWashEnd);
    tlog("                    소독 시작 %u · 소독 종료 %u · 서버 덤프 %u\n",
         (unsigned)opDisStart, (unsigned)opDisEnd, (unsigned)opDump);

    // 양성대조 먼저 — 0 이면 아무것도 안 본 것이다(헛초록 방지)
    CHECK(opMgr > 0 && opWashStart > 0 && opWashEnd > 0 && opDisStart > 0 && opDisEnd > 0 && opDump > 0,
          "접촉 예산 양성대조: 여섯 조작 모두 카드 동작이 세어졌다");
    CHECK(opMgr <= 15 && opWashStart <= 40 && opWashEnd <= 30, "접촉 예산: 담당자·세척 상한 안");
    CHECK(opDisStart <= 60 && opDisEnd <= 30, "접촉 예산: 소독 상한 안");
    CHECK(opDump <= 90, "접촉 예산: 서버 덤프 상한 안(가장 무거운 조작)");

    done();
    for (;;) {}
}
