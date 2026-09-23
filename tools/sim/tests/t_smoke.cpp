// 시험 장치 자체 확인: 세척기 정상 전주기가 실기와 같은 결과를 내는가.
#include "common.h"

static SimCard mgr, scope;

int main()
{
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));
    boot('W');
    CHECK(deviceType == 'W', "boot: 타입 W");
    CHECK(lcd_has("W"), "boot: LCD 출력이 잡힌다");

    // [3차 G] 담당자 미등록(비일회성)이면 세척 시작을 거부한다 — 완전 초기화 직후 상태
    {
        static SimCard early;
        make_tag(early, 0x03, SCOPE_TYPE_TAG, 3, "SC0003", "S0003");
        logs_clear();
        touch(early);
        CHECK(lcd_has("No Manager Info") && get_process(early).WashingStatus == 0 && !rtc.HasAlarm(1),
              "담당자 미등록 → 세척 시작 거부(기록·알람 없음)");
    }

    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);
    CHECK(managerOption.HasData(), "매니저 태그 등록");

    make_tag(scope, 0x02, SCOPE_TYPE_TAG, 29, "SC0029", "SER0029");
    logs_clear();
    touch(scope);
    Process p = get_process(scope);
    tlog("  process: S=%u WS=%u RW=%u MN=%d\n", p.Status, p.WashingStatus, p.Rewrite, p.MachineNumber);
    CHECK(p.WashingStatus == 1 && p.Rewrite == 1, "세척 시작: WS=1 RW=1");
    LocalDateTime st = get_ldt(scope, SECTOR2_WASHING_START);
    tlog_ldt("start", st);
    CHECK(st.Date.Year == 2026 && st.Time.Hour == 10 && st.Time.Minute == 0, "세척 시작 시각 = RTC");
    LocalDateTime en = get_ldt(scope, SECTOR3_WASHING_END);
    tlog_ldt("auto end", en);
    CHECK(rtc.HasAlarm(1), "세척 알람 등록");
    tlog("  buzz50=%u buzz100=%u lcd=[%s]\n", buzz_count(50), buzz_count(100), g_lcdLog);

    sim_advance_ms(10UL * 60 * 1000);
    logs_clear();
    touch(scope);
    en = get_ldt(scope, SECTOR3_WASHING_END);
    tlog_ldt("end", en);
    CHECK(en.Time.Minute >= 10, "세척 종료 = 종료 터치 시각");
    CHECK(!rtc.HasAlarm(1), "세척 알람 해제");
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
