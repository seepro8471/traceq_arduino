// 리더 모듈(MFRC522) 상태 — PC 알림(사장님 수락 10-07 · SeePro 세션 요청)과 재초기화(18차 재설계).
//  알림: 부팅 1회 + PC 에 마지막으로 보낸 상태와 달라질 때. 살아 있으면 `Firmware Version: 0x92 = v2.0` 꼴, 죽었으면(0x00·0xFF) 값과 무관하게
//        `Firmware Version: 0x0 = (unknown)` + `WARNING: Communication failure…`(올눈·델파이는 그 글자와 정확히 같을 때만 음성). 죽음 줄은 5초에 한 번까지.
//  판정과 보고는 **같은 읽기**(한 loop 에 VersionReg 1회) — 따로 읽으면 콘넥터가 떨릴 때 줄과 상태가 어긋난 채 굳었다(⑨⑩).
//  재초기화: 죽은 동안 · **살아난 첫 loop**(전원을 잃었다 온 칩은 안테나 OFF — ⑫) · 설정이 날아간 때(죽음을 못 본 순단 — ⑬).
#include "common.h"

static uint8_t serial_count(const char *s)
{
    uint8_t n = 0;
    for (const char *p = g_serialOut; (p = strstr(p, s)) != nullptr; p += strlen(s)) ++n;
    return n;
}
// PC 가 마지막으로 받은 줄이 말하는 상태: 1 = 정상 줄 · 0 = 죽음 줄 · -1 = 줄 없음
static int8_t last_told()
{
    const char *dead = nullptr, *good = nullptr;
    for (const char *p = g_serialOut; (p = strstr(p, "= (unknown)")) != nullptr; ++p) dead = p;
    for (const char *p = g_serialOut; (p = strstr(p, "= v2.0")) != nullptr; ++p) good = p;
    if (dead == nullptr && good == nullptr) return -1;
    return (good > dead) ? 1 : 0;
}
static bool silent()
{
    return buzz_count(30) + buzz_count(40) + buzz_count(50) + buzz_count(100) + buzz_count(150) + buzz_count(300) +
           buzz_count(400) + buzz_count(500) + buzz_count(600) + buzz_count(1000) == 0;
}
static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};
static void fresh(uint8_t uid, int no)
{
    make_tag(sc, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    g_versionReg = 0x92;
    boot('G');

    // ── ① 부팅: 정상 버전 줄 1회 · WARNING 없음 ──
    tlog("  ① 부팅: 버전줄=%u 경고줄=%u [%.40s]\n", serial_count("Firmware Version: 0x"), serial_count("WARNING: Communication failure"), g_serialOut);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0\r\n") == 1 && serial_count("WARNING") == 0,
          "① 부팅 때 리더 버전 줄이 정확히 1회 나간다(줄 종단 CRLF · 살아 있으면 WARNING 없음)");

    // ── ② 살아 있는 채 루프를 돌아도 더 안 찍는다 · VersionReg 는 loop 당 1회 · 재초기화(소프트 리셋)도 없다 ──
    logs_clear();
    g_versionReads = 0;
    const uint16_t drop0 = g_fieldDrop;
    run_loops(20);
    tlog("  ② 20 loop: VersionReg 읽기=%u 소프트리셋=%u\n", g_versionReads, (unsigned)(g_fieldDrop - drop0));
    CHECK(serial_count("Firmware Version") == 0, "② 상태가 안 바뀌면 루프마다 찍지 않는다(PC 로그 보호)");
    CHECK(g_versionReads == 20 && g_fieldDrop == drop0, "②b 살아 있는 loop 는 VersionReg 를 한 번만 읽고 재초기화하지 않는다");

    // ── ③ 죽음: 0x0 + WARNING 줄 1회 · 'R-X' · 계속 죽어 있어도 1회뿐 · 그 loop 의 읽기도 1회(판정 = 보고) · 무음 ──
    logs_clear(); buzz_clear();
    g_versionReg = 0x00;
    g_versionReads = 0;
    run_loops(1);
    const uint16_t readsEdge = g_versionReads;
    run_loops(9);
    tlog("  ③ 죽음: 에지 loop 읽기=%u 0x0줄=%u 경고줄=%u R-X=%d [%.80s]\n", readsEdge, serial_count("Firmware Version: 0x0 = (unknown)"),
         serial_count("WARNING: Communication failure, is the MFRC522 properly connected?"), lcd_has("R-X"), g_serialOut);
    CHECK(serial_count("Firmware Version: 0x0 = (unknown)") == 1 &&
          serial_count("WARNING: Communication failure, is the MFRC522 properly connected?") == 1 && lcd_has("R-X"),
          "③ 리더가 죽으면 0x0 = (unknown) + WARNING 줄이 정확히 1회(올눈·SeePro·세척관리가 이 줄로 안내) · 화면 R-X");
    CHECK(serial_count("Firmware Version: 0x0 = (unknown)\r\nWARNING: Communication failure, is the MFRC522 properly connected?\r\n") == 1,
          "③d 죽음 알림은 버전 줄 → WARNING 줄 순서 · 각 줄 CRLF 종단(PC 는 줄 단위로 읽는다 · 18차 잠금)");
    CHECK(readsEdge == 1, "③b 에지 loop 도 VersionReg 는 한 번만 읽는다 — 판정한 값을 그대로 보낸다(18차)");
    CHECK(silent(), "③c 리더 알림은 무음이다(부저 0)");

    // ── ④ 복구: 정상 버전 줄 1회(바로) · WARNING 없음 · 그 뒤 조용 ──
    logs_clear();
    g_versionReg = 0x92;
    run_loops(10);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0\r\n") == 1 && serial_count("WARNING") == 0,
          "④ 복구되면 정상 버전 줄 1회(SeePro·세척관리는 0x0/0xFF 아니면 고장으로 안 본다)");

    // ── ⑤ 0xFF(SPI 열림)로 죽어도 글자는 `0x0 = (unknown)` — 올눈·델파이는 그 글자와 정확히 같을 때만 음성을 낸다 ──
    sim_advance_ms(6000);                                 // 죽음 줄 간격(5초)을 넘긴다
    logs_clear();
    g_versionReg = 0xFF;
    run_loops(5);
    tlog("  ⑤ 0xFF 죽음: [%.90s]\n", g_serialOut);
    CHECK(serial_count("Firmware Version: 0x0 = (unknown)") == 1 && serial_count("WARNING") == 1 && serial_count("0xFF") == 0,
          "⑤ 0xFF 로 죽어도 죽음 줄은 `0x0 = (unknown)` + WARNING(값과 무관 · 18차)");
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

    // ── ⑦ 다른 타입도 부팅 줄 1회 — 세척관리 수신부는 이 줄을 읽어 모듈 상태를 안다(① 이 게이트웨이 · 여기서 소독기·서버·세척기) ──
    {
        const char others[] = {'D', 'S'};
        bool both = true;
        for (uint8_t i = 0; i < 2; ++i)
        {
            logs_clear();
            power_restore(); deviceOption.SetType(others[i]); hard_reset(false, 2);
            if (serial_count("Firmware Version: 0x92 = v2.0\r\n") != 1 || serial_count("WARNING") != 0) both = false;
        }
        CHECK(both, "⑦a 소독기·서버 타입도 부팅 때 버전 줄 1회");
    }
    logs_clear();
    power_restore(); deviceOption.SetType('W'); hard_reset(false, 2);
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    CHECK(serial_count("Firmware Version: 0x92 = v2.0") == 1, "⑦ 세척기 타입도 부팅 때 버전 줄 1회(모든 타입)");

    // ── ⑧ 상태 변화가 없는 접촉 처리에는 줄이 끼지 않는다 ──
    logs_clear();
    fresh(0x21, 21);
    touch(sc);
    CHECK(get_process(sc).WashingStatus == 1 && serial_count("Firmware Version") == 0,
          "⑧ 접촉은 처리되고(세척 시작) 버전 줄은 끼지 않는다");

    // ── ⑨ 죽는 순간 다음 읽기가 한 번 정상으로 튀어도 PC 가 마지막으로 듣는 말은 '죽음' 이다 ──
    //  종전(2.2.34~35): 판정 읽기 0x00 · 보고 읽기 0x92 → PC 는 정상 줄만 받고 그 뒤 에지가 없어 영영 무음(리더는 R-X).
    sim_advance_ms(6000);
    logs_clear();
    g_versionReg = 0x00;
    sim_version_seq(0x00, 0x92);
    run_loops(30);
    sim_advance_ms(6000); run_loops(3);
    tlog("  ⑨ 죽음+튐: WARNING=%u 정상줄=%u 마지막=%d R-X=%d\n", serial_count("WARNING"), serial_count("= v2.0"), last_told(), lcd_has("R-X"));
    CHECK(serial_count("WARNING") >= 1 && last_told() == 0,
          "⑨(18차) 죽은 리더 — 읽기가 한 번 튀어도 PC 의 마지막 줄은 죽음 줄이다(보낸 상태 기준으로 수렴)");

    // ── ⑩ 살아나는 순간 다음 읽기가 한 번 죽음으로 튀어도 PC 가 마지막으로 듣는 말은 '정상' 이다 ──
    //  종전: 판정 읽기 0x92 · 보고 읽기 0x00 → PC 는 WARNING 만 받고 영영 정정이 없다(세척관리가 5초마다 08 안내 · 뽑았다 꽂아야 멎음).
    logs_clear();
    g_versionReg = 0x92;
    sim_version_seq(0x92, 0x00);
    run_loops(30);
    sim_advance_ms(6000); run_loops(3);
    tlog("  ⑩ 회복+튐: WARNING=%u 정상줄=%u 마지막=%d R-O=%d\n", serial_count("WARNING"), serial_count("= v2.0"), last_told(), lcd_has("R-O"));
    CHECK(serial_count("= v2.0") >= 1 && last_told() == 1,
          "⑩(18차) 살아난 리더 — 읽기가 한 번 튀어도 PC 의 마지막 줄은 정상 줄이다");

    // ── ⑪ 콘넥터가 떨려도(loop 마다 죽음·정상이 번갈아) 죽음 줄은 5초에 한 번까지 — 올눈이 줄마다 음성·알림창을 쌓지 않게 ──
    sim_advance_ms(6000);
    logs_clear();
    const uint32_t t0 = g_ms;
    uint8_t n = 0;
    while (g_ms - t0 < 4000UL && n < 200) { g_versionReg = (n & 1) ? 0x92 : 0x00; run_loops(1); ++n; }
    const uint8_t deadLines = serial_count("WARNING");
    g_versionReg = 0x92; run_loops(3);
    tlog("  ⑪ 떨림 %u loop/%lums: 죽음줄=%u 정상줄=%u 끝=%d\n", n, (unsigned long)(g_ms - t0), deadLines, serial_count("= v2.0"), last_told());
    CHECK(deadLines == 1 && last_told() == 1, "⑪(18차) 4초 동안 떨려도 죽음 줄은 1회뿐이고, 멎은 뒤 PC 의 마지막 줄은 지금 상태(정상)다");

    // ── ⑫ 전원을 잃었다 돌아온 모듈 — 살아난 첫 loop 가 칩을 다시 초기화해 태그를 읽는다 ──
    //  종전(2.0~2.2.35): 재초기화가 죽은 loop 에서만 나가(칩은 못 듣는다) 살아난 뒤 안테나가 꺼진 채 — R-O 인데 재부팅 전까지 태그 무반응.
    sim_advance_ms(6000);
    g_versionReg = 0x00; run_loops(3);                    // 콘넥터 빠짐
    sim_reader_power_cycle();                             // 다시 꽂음 — 칩은 리셋값(안테나 OFF)
    g_versionReg = 0x92;
    logs_clear();
    run_loops(2);
    fresh(0x22, 22);
    touch(sc);
    tlog("  ⑫ 전원 재인가 뒤 접촉: WS=%u R-O=%d 복구줄=%u\n", get_process(sc).WashingStatus, lcd_has("R-O"), serial_count("= v2.0"));
    CHECK(get_process(sc).WashingStatus == 1,
          "⑫(18차) 전원을 잃었다 돌아온 리더가 태그를 다시 읽는다(살아난 첫 loop 의 재초기화 — 종전엔 R-O 인데 무반응)");

    // ── ⑬ 죽음을 못 본 순단(막힘 사이에 칩만 리셋) — 설정 비트로 알아 조용히 다시 초기화한다(PC 줄 없음) ──
    logs_clear();
    sim_reader_power_cycle();                             // VersionReg 는 계속 0x92
    run_loops(2);
    fresh(0x23, 23);
    touch(sc);
    tlog("  ⑬ 못 본 순단 뒤 접촉: WS=%u 버전줄=%u\n", get_process(sc).WashingStatus, serial_count("Firmware Version"));
    CHECK(get_process(sc).WashingStatus == 1 && serial_count("Firmware Version") == 0,
          "⑬(18차) 죽음 표본 없이 칩만 리셋돼도 다음 loop 가 다시 초기화해 태그를 읽는다(PC 알림 없음)");

    // ── ⑮ 죽은 loop 의 재초기화가 살아나는 순간에 걸쳐 끝자락(안테나 켜기)만 닿은 칩 — 설정 비트는 켜져 있어도 살아난 첫 loop 가 다시 초기화한다 ──
    //  (⑫ 는 설정 비트 검사와 겹쳐 "살아난 첫 loop" 조건만 빼도 초록이었다 — 이 표본이 그 조건을 따로 문다)
    sim_advance_ms(6000);
    g_versionReg = 0x00; run_loops(3);                    // 콘넥터 빠짐
    sim_reader_half_init();                               // 꽂히는 순간: 안테나 비트만 켜짐 · 나머지 설정은 리셋값
    g_versionReg = 0x92;
    logs_clear();
    run_loops(2);
    fresh(0x24, 24);
    touch(sc);
    tlog("  ⑮ 반만 초기화된 칩 뒤 접촉: WS=%u\n", get_process(sc).WashingStatus);
    CHECK(get_process(sc).WashingStatus == 1,
          "⑮(18차) 재초기화가 반만 닿은 칩도 살아난 첫 loop 가 다시 초기화해 태그를 읽는다(설정 비트 검사만으로는 못 잡는다)");

    // ── ⑭ 살아 있는 칩의 줄 글자(라이브러리와 같다) — 호환 칩·옛 판·모르는 판 ──
    {
        struct { uint8_t v; const char *line; } k[] = {
            {0x88, "Firmware Version: 0x88 = (clone)\r\n"}, {0x90, "Firmware Version: 0x90 = v0.0\r\n"},
            {0x91, "Firmware Version: 0x91 = v1.0\r\n"},    {0x12, "Firmware Version: 0x12 = (unknown)\r\n"}};
        bool all = true;
        for (uint8_t i = 0; i < 4; ++i)
        {
            logs_clear();
            g_versionReg = k[i].v;
            hard_reset(false, 2);
            const bool ok = serial_count(k[i].line) == 1 && serial_count("WARNING") == 0;
            tlog("  ⑭ 0x%02X: %d [%.34s]\n", k[i].v, ok, g_serialOut);
            if (!ok) all = false;
        }
        CHECK(all, "⑭ 살아 있는 칩은 실제 값 그대로(0x88 clone · 0x90 v0.0 · 0x91 v1.0 · 모르는 값 (unknown)) — WARNING 없음");
        g_versionReg = 0x92;
    }
    done();
    for (;;) {}
}
