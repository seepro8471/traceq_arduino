// II-H 제안 잠금 8 — v2.2.14 기기번호 3자리 표시(15열부터 · 20열 안). t_a8 V1 은 `lcd_has`(출력 **기록**)로 보므로
//  한 칸 밀려 20열 밖으로 떨어진 글자도 기록에는 남아 초록이었다(헛초록). **화면 격자**(lcd_row)로 본다.
//  같은 CHECK 의 `!lcd_has("WW:05")` 는 출력 기록에 나올 수 없는 문자열이라 **실패할 수 없는 CHECK** 다 — 격자로 바꾼다.
#include "common.h"

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    deviceOption.SetNumber(120);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    tlog("  3행 격자=[%s]\n", lcd_row(3));
    CHECK(strncmp(lcd_row(3) + 15, "W:120", 5) == 0, "T 기기번호 3자리는 화면 15~19열에 온전히 보인다(20열 밖으로 안 밀림)");
    deviceOption.SetNumber(5);
    ui.InvalidateHome(); logs_clear(); run_loops(1);
    tlog("  3행 격자=[%s]\n", lcd_row(3));
    CHECK(strncmp(lcd_row(3) + 15, " W:05", 5) == 0, "T2 3자리→2자리로 줄어도 15~19열이 \" W:05\"(잔상 없음 · 화면 격자로)");
    done();
    for (;;) {}
}
