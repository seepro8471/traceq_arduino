// 리더 모듈(MFRC522) 상태 시리얼 알림(사장님 수락 10-07 · SeePro 세션 요청) — 부팅 1회 + 살아있음↔죽음 에지에서만
//  `PCD_DumpVersionToSerial()` 줄 그대로. 죽음 = `Firmware Version: 0x0 = (unknown)` + `WARNING: Communication failure…`(올눈·SeePro 음성 8번) ·
//  복구 = 정상 버전 줄. 매 루프 찍지 않는다(에지) · 부팅 때 죽어 있어도 setup 1회 뒤 첫 loop 가 또 찍지 않는다 · 모든 타입.
#include "common.h"

static uint8_t serial_count(const char *s)
{
    uint8_t n = 0;
    for (const char *p = g_serialOut; (p = strstr(p, s)) != nullptr; p += strlen(s)) ++n;
    return n;
}
static SimCard sc;

int main()
{
    rtc_set(rel_date(10, 0, 0));
    g_versionReg = 0x92;
    boot('G');

    // ── ① 부팅: 정상 버전 줄 1회 · WARNING 없음 ──
    tlog("  ① 부팅: 버전줄=%u 경고줄=%u [%.40s]\n", serial_count("Firmware Version: 0x"), serial_count("WARNING: Communication failure"), g_serialOut);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0") == 1 && serial_count("WARNING") == 0,
          "① 부팅 때 리더 버전 줄이 정확히 1회 나간다(살아 있으면 WARNING 없음)");

    // ── ② 살아 있는 채 루프를 돌아도 더 안 찍는다(에지) ──
    logs_clear();
    run_loops(20);
    CHECK(serial_count("Firmware Version") == 0, "② 상태가 안 바뀌면 루프마다 찍지 않는다(에지 · PC 로그 보호)");

    // ── ③ 죽음: 0x0 + WARNING 줄 1회 · 'R-X' · 계속 죽어 있어도 1회뿐 ──
    logs_clear();
    g_versionReg = 0x00;
    run_loops(10);
    tlog("  ③ 죽음: 0x0줄=%u 경고줄=%u R-X=%d [%.80s]\n", serial_count("Firmware Version: 0x0 = (unknown)"),
         serial_count("WARNING: Communication failure, is the MFRC522 properly connected?"), lcd_has("R-X"), g_serialOut);
    CHECK(serial_count("Firmware Version: 0x0 = (unknown)") == 1 &&
          serial_count("WARNING: Communication failure, is the MFRC522 properly connected?") == 1 && lcd_has("R-X"),
          "③ 리더가 죽으면 0x0 = (unknown) + WARNING 줄이 정확히 1회(올눈·SeePro 가 이 줄로 음성 8번) · 화면 R-X");

    // ── ④ 복구: 정상 버전 줄 1회 · WARNING 없음 · 그 뒤 조용 ──
    logs_clear();
    g_versionReg = 0x92;
    run_loops(10);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0") == 1 && serial_count("WARNING") == 0,
          "④ 복구되면 정상 버전 줄 1회(SeePro 는 0x0/0xFF 아니면 고장으로 안 본다)");

    // ── ⑤ 0xFF(SPI 열림)도 죽음 ──
    logs_clear();
    g_versionReg = 0xFF;
    run_loops(5);
    CHECK(serial_count("Firmware Version: 0xFF = (unknown)") == 1 && serial_count("WARNING") == 1, "⑤ 0xFF 도 죽음 줄(SPI 열림)");
    g_versionReg = 0x92; run_loops(5);

    // ── ⑥ 부팅 때 죽어 있으면 setup 이 1회 알리고 첫 loop 가 또 찍지 않는다 ──
    logs_clear();                     // hard_reset 은 시리얼 로그를 안 비운다 — 앞 단계 줄이 겹쳐 세지 않게
    g_versionReg = 0x00;
    hard_reset(false, 3);
    tlog("  ⑥ 죽은 채 부팅: 0x0줄=%u 경고줄=%u\n", serial_count("Firmware Version: 0x0 = (unknown)"), serial_count("WARNING"));
    CHECK(serial_count("Firmware Version: 0x0 = (unknown)") == 1 && serial_count("WARNING") == 1,
          "⑥ 죽은 채 부팅하면 setup 의 1회뿐 — 첫 loop 가 같은 줄을 또 찍지 않는다(부팅 상태를 먼저 잰다)");
    logs_clear(); g_versionReg = 0x92; run_loops(3);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0") == 1, "⑥b 그 뒤 복구 줄 1회");

    // ── ⑦ 다른 타입(세척기)도 같다 — 세척관리 수신부는 모르는 줄을 로그만 남긴다(세척관리 세션 확인 10-07) ──
    logs_clear();
    power_restore(); deviceOption.SetType('W'); hard_reset(false, 2);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0") == 1, "⑦ 세척기 타입도 부팅 때 버전 줄 1회(모든 타입)");

    // ── ⑧ 접촉 처리 중엔 끼지 않는다 — 살아 있는 태그 접촉(세척 시작)의 시리얼 출력에 버전 줄이 없다 ──
    logs_clear();
    make_tag(sc, 0x21, SCOPE_TYPE_TAG, 21, "SC21", "SER");
    touch(sc);
    CHECK(serial_count("Firmware Version") == 0, "⑧ 상태 변화가 없는 접촉 처리에는 버전 줄이 끼지 않는다");
    done();
    for (;;) {}
}
