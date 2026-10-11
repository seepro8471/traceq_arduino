// 기기 번호는 3자리(사장님 10-11 "세척기와 소독기의 번호를 3자리로") — 화면·메뉴·범위.
//  (II-H 제안 잠금 8 에서 시작: t_a8 V1 은 `lcd_has`(출력 **기록**)로 보므로 한 칸 밀려 20열 밖으로 떨어진 글자도 기록에는 남아
//   초록이었다(헛초록) → **화면 격자**(lcd_row)로 본다.)
#include "common.h"

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');

    // ── T: 홈 3행 15~19열 = `타입:NNN`(늘 5칸 · 0 채움) — 네 타입 전부 ──
    deviceOption.SetNumber(120);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    tlog("  3행 격자=[%s]\n", lcd_row(3));
    CHECK(strncmp(lcd_row(3) + 15, "W:120", 5) == 0, "T 기기번호 3자리는 화면 15~19열에 온전히 보인다(20열 밖으로 안 밀림)");
    deviceOption.SetNumber(5);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    tlog("  3행 격자=[%s]\n", lcd_row(3));
    CHECK(strncmp(lcd_row(3) + 15, "W:005", 5) == 0, "T2 한 자리 번호도 3자리로 채운다(W:005 · 줄어도 잔상 없음 · 화면 격자로)");
    deviceOption.SetNumber(0);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    CHECK(strncmp(lcd_row(3) + 15, "W:000", 5) == 0, "T3 번호 0 은 W:000");
    deviceOption.SetNumber(999);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    CHECK(strncmp(lcd_row(3) + 15, "W:999", 5) == 0, "T4 상한 999 는 W:999");
    {
        const char types[] = {'D', 'G', 'S'};
        bool all = true;
        for (uint8_t i = 0; i < 3; ++i)
        {
            power_restore(); deviceOption.SetType(types[i]); deviceOption.SetNumber(7); hard_reset(false, 2);
            char want[6]; snprintf(want, sizeof(want), "%c:007", types[i]);
            tlog("  [%c] 3행 격자=[%s]\n", types[i], lcd_row(3));
            if (strncmp(lcd_row(3) + 15, want, 5) != 0) all = false;
        }
        CHECK(all, "T5 소독기·게이트웨이·서버도 같은 3자리 꼴(D:007 · G:007 · S:007)");
        power_restore(); deviceOption.SetType('D'); hard_reset(false, 2);
    }

    // ── M: 메뉴의 번호 편집은 3자리 — 제목 `Number (007)` · 백의 자리를 고칠 수 있다(종전 2자리 편집은 100 이상을 못 넣었다) ──
    {
        deviceOption.SetNumber(7);
        g_btnIdleLimit = 1000000UL;
        logs_clear();
        buttons_script("SRS" "LS");                       // 첫 자리(백의 자리) 편집 → +1 → 편집 끝 → 저장 칸 → 저장
        GUARDED(ui.SetDeviceNumber(deviceOption));
        tlog("  M 007 의 첫 자리 +1 → %d · 제목=%d\n", deviceOption.GetNumber(), lcd_has("Number (007)"));
        CHECK(lcd_has("Number (007)"), "M 번호 메뉴 제목은 3자리(Number (007))");
        CHECK(deviceOption.GetNumber() == 107, "M2 첫 자리는 백의 자리다 — 007 → 107(2자리 편집이면 17 이 된다)");
        deviceOption.SetNumber(999);
        logs_clear();
        buttons_script("SRS" "LS");                       // 9 → 0
        GUARDED(ui.SetDeviceNumber(deviceOption));
        CHECK(deviceOption.GetNumber() == 99, "M3 백의 자리 9 에서 +1 은 0 으로 돈다(999 → 099)");
        deviceOption.SetNumber(300);
        logs_clear();
        buttons_script("LS");                             // 그대로 저장
        GUARDED(ui.SetDeviceNumber(deviceOption));
        CHECK(deviceOption.GetNumber() == 300 && lcd_has("Number (300)"), "M4 고치지 않고 저장하면 그대로(300)");
        deviceOption.SetNumber(1);
    }
    done();
    for (;;) {}
}
