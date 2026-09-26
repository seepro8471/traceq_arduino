// 메뉴 무조작 시한(09-26 · 사장님 09-25 판정) 잠금 — 60초 무조작이면 저장 없이 홈으로 나오고, 그 뒤 태그를 읽는다.
// 시한이 없으면 GUARDED 가 2(버튼 무한 대기)를 돌려준다 — 그것이 빨강이다.
#include "common.h"

static SimCard mgr, a;

static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 26, 9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, DateTime(2026, 9, 26, 9, 4, 0));
}

int main()
{
    rtc_set(DateTime(2026, 9, 26, 10, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");
    touch(mgr);
    g_btnIdleLimit = 1000000UL;                  // 60초(≈18만 회 읽기)가 하네스 한도에 먼저 안 걸리게

    // ── ① 메뉴 첫 화면에서 무조작 → 60초에 스스로 나온다(소프트 리셋 없이) ──
    {
        buttons_script("S");                     // MENU 한 번 → 메뉴 진입, 그 뒤 무조작
        const unsigned long t0 = millis();
        const int r = GUARDED(loop());
        const unsigned long dt = millis() - t0;
        tlog("  ①메뉴 무조작: r=%d 경과=%lums resets=%u\n", r, dt, g_resetCount);
        CHECK(r == 0, "①메뉴 무조작 → 버튼 무한 대기(2) 없이 loop 이 돌아온다");
        CHECK(dt >= 60000UL && dt < 75000UL, "①메뉴 무조작 → 약 60초에 나온다");
        CHECK(g_resetCount == 0, "①시한 탈출은 소프트 리셋을 일으키지 않는다");
    }

    // ── ② 나온 뒤에는 태그를 정상으로 읽는다(메뉴에 갇혀 있지 않다) ──
    {
        washed_scope(a, 0x21, 21);
        logs_clear();
        touch(a);
        tlog("  ②시한 뒤 태그: RW=%u\n", get_process(a).Rewrite);
        CHECK(get_process(a).Rewrite == 2, "②시한으로 나온 뒤 스코프 태그가 처리된다(소독 시작)");
    }

    // ── ③ 편집 중 무조작 → 저장 없이 나온다(값 그대로) ──
    {
        disinfectionOption.SetMaximumCount(30);
        buttons_script("SRS" "RSRRRRR");         // 편집 진입·자릿수 바꿈(45 직전) … 저장 없이 방치
        const unsigned long t0 = millis();
        const int r = GUARDED(ui.SetDisinfectionMaximumCount(disinfectionOption));
        tlog("  ③편집 중 방치: r=%d 경과=%lums MaxCount=%d resets=%u\n", r, millis() - t0,
             disinfectionOption.GetMaximumCount(), g_resetCount);
        CHECK(r == 0 && disinfectionOption.GetMaximumCount() == 30,
              "③편집 중 60초 무조작 → 저장 없이 나오고 값은 그대로(30)");
        CHECK(g_resetCount == 0, "③편집 화면 시한 탈출도 소프트 리셋 없음(Exit 경로)");
    }

    // ── ④ 조작이 이어지면 시한이 늘어난다(마지막 버튼부터 60초) ──
    //    진입 40초 뒤 R 한 번 → 그때부터 60초 → 총 ≈100초에 나와야 한다. 버튼이 시계를 안 갱신하면 60초에 나온다.
    {
        buttons_script("S");
        const unsigned long t0 = millis();
        buttons_at('R', t0 + 40000UL);
        const int r = GUARDED(loop());
        const unsigned long dt = millis() - t0;
        tlog("  ④40초 뒤 R, 그 뒤 무조작: r=%d 경과=%lums\n", r, dt);
        CHECK(r == 0 && dt >= 95000UL && dt < 115000UL,
              "④버튼을 누르면 시한이 그때부터 다시 60초(약 100초에 나옴)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
