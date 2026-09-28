// II-H 제안 잠금 6 — P3 봉합 중 안 잠긴 것. HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  L) v2.2.15 GetNumber 상한 999 — 세터도 999 로 자르므로 모든 시험 표본이 세터를 거쳐 초록. **EEPROM 에 이미 있는**
//     범위 밖 값(1.4.1 유지 업그레이드·찢긴 쓰기)을 게터가 돌려주지 않는지 본다.
//  M) v2.2.15 MaxCount 제목 버퍼 18 — 4자리(9999)에서 닫는 괄호. 형제(Number·Alarm Time)만 잠겨 있었다.
//  N) v2.2.13 손상 세척 시작(연도 0xFFFF)으로 RTC 복구 금지 — t_a7 C⑤ 는 **동기된** 시계에서 재므로 v2.2.23 의
//     IsUnsynced 관문이 복구 자체를 막아 헛초록. 미동기 소독기에서 잰다.
//  O) v2.2.13 cfg_get_config 담당자 키·이름 NUL 자리(+1) — 16바이트가 꽉 찬 담당자로 잰다.
//  P) v2.2.13 str_atoi_range 루프 변수 uint16 — t_a7 E5 는 길이 2 문자열이라 end 가 먼저 잘려 루프가 255 에
//     닿지 않는다(헛초록). 256자 넘는 '0' 문자열로 잰다(uint8 이면 끝나지 않는다 → 멈춤 = 빨강). ★맨 끝에 둔다.
#include "common.h"

static SimCard sc;

static void washed_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC0071", "S0071");
    set_process(t, Process{1, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');

    // ── L) EEPROM 에 범위 밖 기기번호가 이미 있을 때 ──
    {
        EEPROM.put((int)2, (int)1234);
        const int n = deviceOption.GetNumber();
        tlog("  L EEPROM 1234 → GetNumber=%d\n", n);
        CHECK(n == 999, "L 게터는 세터 범위 밖 값(1234)을 돌려주지 않는다(상한 999)");
        deviceOption.SetNumber(1);
    }

    // ── M) MaxCount 제목 4자리 ──
    {
        disinfectionOption.SetMaximumCount(9999);
        logs_clear();
        const int r = GUARDED(ui.SetDisinfectionMaximumCount(disinfectionOption));   // 무조작 60초 → Exit
        tlog("  M r=%d 제목 보임=%d\n", r, (int)lcd_has("Max Count (9999)"));
        CHECK(lcd_has("Max Count (9999)"), "M MaxCount 제목이 4자리에서도 닫는 괄호까지 보인다");
        disinfectionOption.SetMaximumCount(30);
    }

    // ── N) 미동기 소독기 + 손상 세척 시작(연도 0xFFFF) ──
    {
        rtc_set(DateTime(2026, 1, 1, 0, 10, 0));        // 방전 표지 = IsUnsynced
        hard_reset(false, 2);
        unsigned char mk[ManagerOption::KEY_SIZE] = {'M', 'G', 'R', '1'};
        unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};
        managerOption.SetData(mk, mn);                  // 담당자 없으면 is_valid 에서 거부돼 복구 자리에 못 간다
        washed_scope(sc, 0x71, 71);
        memset(sc.data[SECTOR2_WASHING_START], 0xFF, 10);
        touch(sc);
        tlog("  N 미동기+손상 시작 → RTC 년=%u\n", rtc.GetCurrentDateTime().year());
        CHECK(rtc.GetCurrentDateTime().year() == 2026,
              "N 미동기 소독기도 손상 세척 시작(연도 0xFFFF)으로 RTC 를 2047 로 복구하지 않는다");
        rtc_set(rel_date(10, 0, 0));
    }

    // ── O) 16바이트가 꽉 찬 담당자 키·이름 → cfg_get_config ──
    {
        unsigned char k[ManagerOption::KEY_SIZE], nm[ManagerOption::NAME_SIZE];
        memcpy(k, "ABCDEFGHIJKLMNOP", 16);
        memcpy(nm, "QRSTUVWXYZ123456", 16);
        managerOption.SetData(k, nm);
        logs_clear();
        const char js[] = "{\"cmd\":\"cfg_get_config\"}";
        serial_inject(js, sizeof(js) - 1);
        GUARDED(serialEvent());
        tlog("  O key=%d name=%d\n", (int)serial_has("\"manager_key\":\"ABCDEFGHIJKLMNOP\""),
             (int)serial_has("\"manager_name\":\"QRSTUVWXYZ123456\""));
        CHECK(serial_has("\"manager_key\":\"ABCDEFGHIJKLMNOP\"") && serial_has("\"manager_name\":\"QRSTUVWXYZ123456\""),
              "O 16바이트가 꽉 찬 담당자 키·이름도 JSON 에 정확히 16자로 나간다(뒤 메모리를 읽지 않는다)");
    }

    // ── P) str_atoi_range(…, 255) 가 256자 넘는 문자열에서 끝난다 — ★맨 끝(멈추면 뒤가 안 돈다) ──
    {
        static char z[300];
        memset(z, '0', 299);
        z[299] = 0;
        const int v = str_atoi_range(z, 0, 255);
        tlog("  P 299자 '0' → %d\n", v);
        CHECK(v == 0, "P str_atoi_range 의 end=255 가 긴 문자열에서도 끝난다(uint8 루프 무한 금지)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
