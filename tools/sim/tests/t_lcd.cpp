// 홈 1행·R-O 캐시(09-26) 잠금 — 안 바뀐 글자는 다시 안 쓰고, 화면을 지운 뒤엔 반드시 다시 쓴다.
#include "common.h"

static uint8_t count_str(const char *hay, const char *needle)
{
    uint8_t n = 0; const size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + len, needle)) ++n;
    return n;
}

int main()
{
    rtc_set(DateTime(2026, 9, 26, 10, 0, 0));   // 초가 안 바뀌게 고정
    boot('S');                                   // 서버: 1행 = "not connected"(고정 문자열)

    // ── 안 바뀐 1행·R-O 는 여러 루프에 걸쳐 한 번만 그린다 ──
    ui.InvalidateHome();                          // 깨끗한 기준 — 첫 루프가 한 번 그린다
    logs_clear();
    run_loops(10);
    const uint8_t row1 = count_str(g_lcdLog, "not connected");
    const uint8_t ro   = count_str(g_lcdLog, "R-O");
    tlog("  10루프: 'not connected'=%u회 'R-O'=%u회\n", row1, ro);
    CHECK(row1 == 1, "안 바뀐 1행은 10루프에 한 번만 그린다(매 루프 재기록 아님)");
    CHECK(ro == 1, "안 바뀐 R-O 는 10루프에 한 번만 그린다");

    // ── 화면을 지운 뒤(InvalidateHome)에는 같은 내용이라도 반드시 다시 그린다 ──
    ui.InvalidateHome();
    logs_clear();
    run_loops(3);
    tlog("  지운 뒤 3루프: 'not connected'=%u 'R-O'=%u\n",
         count_str(g_lcdLog, "not connected"), count_str(g_lcdLog, "R-O"));
    CHECK(count_str(g_lcdLog, "not connected") == 1, "화면 지운 뒤 1행을 다시 그린다(빈 화면으로 남지 않음)");
    CHECK(count_str(g_lcdLog, "R-O") == 1, "화면 지운 뒤 R-O 를 다시 그린다");

    // ── R-O → R-X → R-O 상태 변화는 매번 그린다 ──
    logs_clear();
    g_versionReg = 0x00; run_loops(1);          // 리더 죽음 → R-X
    g_versionReg = 0x92; run_loops(1);          // 살아남 → R-O
    tlog("  상태 변화: R-X=%u R-O=%u\n", count_str(g_lcdLog, "R-X"), count_str(g_lcdLog, "R-O"));
    CHECK(count_str(g_lcdLog, "R-X") == 1 && count_str(g_lcdLog, "R-O") == 1,
          "R-O↔R-X 상태가 바뀌면 그때마다 그린다");

    // ── 1행 내용이 바뀌면(연결됨) 다시 그린다 ──
    logs_clear();
    serial_inject("Z", 1); GUARDED(serialEvent());   // 인증 → "connected"
    run_loops(2);
    tlog("  인증 뒤: connected=%u\n", count_str(g_lcdLog, "connected"));
    CHECK(count_str(g_lcdLog, "connected") >= 1, "1행 내용이 바뀌면 다시 그린다");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
