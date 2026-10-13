// 리더 모듈(MFRC522) 감시 — PC 알림(사장님 수락 10-07)과 재초기화. 18차 재설계 · 19차 "한 번의 읽기를 믿지 않는다".
//  알림: 부팅 1회 + PC 에 마지막으로 보낸 상태와 달라질 때. 살아 있으면 `Firmware Version: 0x92 = v2.0`, 죽었으면(0x00·0xFF) 값과 무관하게
//        `Firmware Version: 0x0 = (unknown)` + `WARNING: Communication failure…`(올눈·델파이는 그 글자와 정확히 같을 때만 음성).
//        죽음 줄은 **죽은 채 700ms 를 넘겨야** 나가고(튄 읽기 하나에 안 울린다 · 델파이·올눈의 묵음 타이머 333/500ms 안에 종결 줄 뒤로
//        붙지 않는다) 앞 죽음 줄에서 5초가 지나야 한다. 복구 줄은 바로. 판정과 보고는 같은 읽기(한 loop 에 VersionReg 1회).
//  재초기화: 죽음이 이어지는 동안(둘째 loop 부터) · 확인된 죽음 뒤 살아난 첫 loop · 살아 있는데 설정(TMode·TxASK·안테나)이 날아가 보이는 것이
//        두 loop 이어질 때. 설정 때문에 세 번 고쳐도 안 읽히면 그만둔다(되읽기가 안 되는 칩). 읽기 하나로는 아무것도 하지 않는다.
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
static bool silent() { return buzz_total() == 0; }
static void run_ms(uint32_t ms)
{
    const uint32_t t0 = g_ms;
    while (g_ms - t0 < ms) run_loops(1);
}
static const char DEAD2[] = "Firmware Version: 0x0 = (unknown)\r\nWARNING: Communication failure, is the MFRC522 properly connected?\r\n";
static const char ALIVE[] = "Firmware Version: 0x92 = v2.0\r\n";
static SimCard sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', 'H'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'H', 'O', 'N', 'G'};
static uint8_t s_uid = 0x20;
// 새 스코프를 대고 세척이 시작됐는지(= 리더가 태그를 읽는지)
static bool reads_tag()
{
    ++s_uid;
    make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
    set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
    touch(sc);
    return get_process(sc).WashingStatus == 1;
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    g_versionReg = 0x92;
    boot('G');

    // ── ① 부팅: 정상 버전 줄 1회(CRLF) · WARNING 없음 ──
    CHECK(serial_count(ALIVE) == 1 && serial_count("Firmware Version") == 1 && serial_count("WARNING") == 0,
          "① 부팅 때 리더 버전 줄이 정확히 1회 나간다(줄 종단 CRLF · 살아 있으면 WARNING 없음)");
    // 전원을 켠 부팅은 하드 리셋 갈래(RST↑ + 50ms)로 한 번에 초기화된다 — 그 대기를 빼면 쓰기가 버려져 설정 판정이 두 loop 뒤에야 고친다
    CHECK(g_softResets == 0, "①b 전원을 켠 부팅과 첫 두 loop 에는 소프트 리셋(다시 초기화)이 없다");

    // ── ② 살아 있는 채: 줄 없음 · VersionReg 는 loop 당 1회 · 재초기화 없음 ──
    logs_clear();
    g_versionReads = 0;
    uint16_t drop0 = g_fieldDrop, soft0 = g_softResets;
    run_loops(20);
    tlog("  ② 20 loop: VersionReg 읽기=%u 소프트리셋=%u\n", g_versionReads, (unsigned)(g_softResets - soft0));
    CHECK(serial_count("Firmware Version") == 0, "② 상태가 안 바뀌면 루프마다 찍지 않는다(PC 로그 보호)");
    CHECK(g_versionReads == 20 && g_fieldDrop == drop0 && g_softResets == soft0,
          "②b 살아 있는 loop 는 VersionReg 를 한 번만 읽고 재초기화하지 않는다");

    // ── ③ 한 번 튄 읽기(죽음 1 loop)에는 아무것도 하지 않는다 — 줄 0 · 재초기화 0 (19차) ──
    //  18차는 여기서 죽음 줄(= PC 음성 "콘넥터 확인")과 재초기화 2회가 나갔다. 2.2.35 까지는 보고가 다시 읽어 우연히 안 나갔다.
    logs_clear(); buzz_clear();
    drop0 = g_fieldDrop; soft0 = g_softResets;
    sim_version_seq(0x00, 0x92);
    run_loops(1);
    const bool sawRX = lcd_has("R-X");
    run_ms(1500);
    tlog("  ③ 튄 읽기 1회: 줄=%u 소프트리셋=%u 필드끊김=%u R-X=%d\n", serial_count("Firmware Version"), (unsigned)(g_softResets - soft0),
         (unsigned)(g_fieldDrop - drop0), sawRX);
    CHECK(serial_count("Firmware Version") == 0 && serial_count("WARNING") == 0 && g_softResets == soft0 && g_fieldDrop == drop0 && sawRX,
          "③(19차) 튄 읽기 하나에는 PC 줄도 재초기화도 없다(화면만 그 loop 에 R-X)");

    // ── ④ 죽음: 둘째 loop 부터 재초기화 · 죽음 줄은 700ms 를 넘겨 1회 · 순서·CRLF · 무음 · 죽어 있는 동안 되풀이 없음 ──
    logs_clear(); buzz_clear();
    soft0 = g_softResets;
    g_versionReg = 0x00;
    g_versionReads = 0;
    const uint32_t tDead = g_ms;
    run_loops(1);
    const uint16_t softAfter1 = g_softResets - soft0;
    run_loops(1);
    const uint16_t softAfter2 = g_softResets - soft0;
    const uint8_t linesAt2 = serial_count("Firmware Version");
    uint16_t loops = 2;
    while (serial_count("WARNING") == 0 && g_ms - tDead < 3000UL) { run_loops(1); ++loops; }
    const uint32_t lineAt = g_ms - tDead;
    const bool once = serial_count(DEAD2) == 1 && serial_count("Firmware Version") == 1 && serial_count("WARNING") == 1;
    const bool readsOk = g_versionReads == loops;
    run_ms(6000);
    tlog("  ④ 죽음: 재초기화 1loop=%u 2loop=%u · 2loop 째 줄=%u · 죽음 줄 %lums 뒤 · 6초 뒤 줄=%u\n", softAfter1, softAfter2, linesAt2,
         (unsigned long)lineAt, serial_count("Firmware Version"));
    CHECK(softAfter1 == 0 && softAfter2 == 1, "④ 죽음이 이어지면 둘째 loop 부터 재초기화한다(첫 loop 는 아직 믿지 않는다)");
    CHECK(linesAt2 == 0 && lineAt >= 700UL && lineAt < 900UL,
          "④b 죽음 줄은 죽은 채 700ms 를 넘겨야 나간다(접촉 직후 PC 묵음 타이머 333/500ms 안에 붙지 않는다)");
    CHECK(once && lcd_has("R-X"), "④c 죽음 알림은 `0x0 = (unknown)` 줄 → WARNING 줄 순서로 정확히 1회(CRLF) · 화면 R-X");
    CHECK(readsOk, "④d 죽은 loop 도 VersionReg 는 한 번만 읽는다 — 판정한 값을 그대로 보낸다");
    CHECK(serial_count("Firmware Version") == 1 && silent(), "④e 죽어 있는 동안 줄을 되풀이하지 않고, 리더 알림은 무음이다");

    // ── ⑤ 복구: 살아난 첫 loop 에 정상 줄 1회(바로) + 재초기화 ──
    logs_clear();
    drop0 = g_fieldDrop;
    g_versionReg = 0x92;
    run_loops(1);
    const uint8_t aliveAt1 = serial_count(ALIVE);
    run_loops(10);
    CHECK(aliveAt1 == 1 && serial_count("Firmware Version") == 1 && serial_count("WARNING") == 0 && g_fieldDrop == drop0 + 1,
          "⑤ 복구되면 그 loop 에 정상 줄 1회 · 살아난 첫 loop 에 칩을 한 번 다시 초기화한다");

    // ── ⑥ 0xFF(SPI 열림)로 죽어도 글자는 `0x0 = (unknown)` ──
    logs_clear();
    g_versionReg = 0xFF;
    run_ms(1500);
    tlog("  ⑥ 0xFF 죽음: [%.90s]\n", g_serialOut);
    CHECK(serial_count(DEAD2) == 1 && serial_count("0xFF") == 0 && serial_count("Firmware Version") == 1,
          "⑥ 0xFF 로 죽어도 죽음 줄은 `0x0 = (unknown)` + WARNING(값과 무관)");

    // ⑥b 0xFF 죽음 loop 는 소프트 리셋의 완료 대기 상한(150ms)에서 끝난다 — 기다림이 없어도, 더 기다려도 빨강
    {
        const uint32_t t0 = g_ms;
        run_loops(1);
        const uint32_t dt = g_ms - t0 - 30;
        tlog("  ⑥b 0xFF 죽음 loop %lums\n", (unsigned long)dt);
        CHECK(dt >= 150UL && dt < 600UL, "⑥b 0xFF 죽음 loop 는 소프트 리셋 대기 상한 150ms 안팎이다(상한을 늘리면 죽은 동안 화면이 굳는다)");
    }

    // ── ⑦ 죽음 줄은 5초에 한 번까지 — 방금 죽음 줄이 나갔으면 다시 죽어도 5초 뒤에야 ──
    const uint32_t tLine = g_ms;                          // 위 ⑥ 의 죽음 줄은 이보다 앞(0.8초쯤 전)
    g_versionReg = 0x92; run_loops(2);                    // 복구(정상 줄)
    logs_clear();
    g_versionReg = 0x00;
    run_ms(2000);                                         // 죽은 지 2초 — 확정은 됐지만 앞 죽음 줄에서 5초가 안 됐다
    const uint8_t early = serial_count("WARNING");
    run_ms(4000);
    tlog("  ⑦ 간격: 다시 죽은 지 2초 줄=%u → %lums 뒤 줄=%u\n", early, (unsigned long)(g_ms - tLine), serial_count("WARNING"));
    CHECK(early == 0 && serial_count("WARNING") == 1, "⑦ 죽음 줄은 앞 죽음 줄에서 5초가 지나야 나간다(떨려도 올눈이 음성·알림창을 쌓지 않게)");
    g_versionReg = 0x92; run_loops(3);

    // ── ⑧ 죽은 채 부팅: setup 이 1회 알리고(부팅은 죽음이면 한 번 더 읽는다) loop 가 또 찍지 않는다 → 복구 줄 ──
    logs_clear();
    g_versionReg = 0x00;
    g_versionReads = 0;
    hard_reset(false, 0);
    const uint16_t bootReads = g_versionReads;
    const uint32_t tBoot = g_ms;
    run_loops(3);
    CHECK(serial_count(DEAD2) == 1 && serial_count("Firmware Version") == 1 && bootReads == 2,
          "⑧ 죽은 채 부팅하면 setup 의 1회뿐 — 첫 loop 가 같은 줄을 또 찍지 않는다(부팅은 죽음이면 한 번 더 읽어 확인)");
    logs_clear();
    drop0 = g_fieldDrop;
    g_versionReg = 0x92; run_loops(3);
    CHECK(serial_count(ALIVE) == 1 && g_fieldDrop == drop0 + 1, "⑧b 죽은 채 부팅했다 살아나면 복구 줄 1회 + 그 loop 에 다시 초기화");
    // ⑧c 죽은 채 부팅 → 살아남 → 곧 다시 죽음: 부팅 죽음 줄에서 5초가 지나야 다음 죽음 줄(부팅 줄도 간격에 든다)
    logs_clear();
    g_versionReg = 0x00;
    while (g_ms - tBoot < 3000UL) run_loops(1);
    const uint8_t at3s = serial_count("WARNING");
    while (g_ms - tBoot < 6000UL) run_loops(1);
    tlog("  ⑧c 부팅 죽음 줄 뒤 다시 죽음: 3초 줄=%u 6초 줄=%u\n", at3s, serial_count("WARNING"));
    CHECK(at3s == 0 && serial_count("WARNING") == 1, "⑧c 죽은 채 부팅한 죽음 줄도 5초 간격에 든다(살아났다 곧 다시 죽어도 5초 뒤에)");
    g_versionReg = 0x92; run_loops(3);

    // ⑧d 죽은 채 부팅한 것은 이미 확인된 죽음이다 — 바로 다음 loop 에 살아나도 그 loop 에 다시 초기화하고 복구 줄을 낸다.
    //  부팅 직전에 태그 접촉이 있었어도 같다(setup 이 접촉 뒤 묵음 시각을 다시 놓는다)
    ++s_uid;
    make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
    card_place(&sc); run_loops(2); card_remove();        // 접촉 + 그 다음 loop(묵음 시각이 방금으로 찍힌다)
    logs_clear();
    g_versionReg = 0x00;
    hard_reset(false, 0);
    g_versionReg = 0x92;
    soft0 = g_softResets;
    run_loops(1);
    CHECK(g_softResets == soft0 + 1 && serial_count(ALIVE) == 1, "⑧d 죽은 채 부팅 직후 첫 loop 에 살아나면 그 loop 에 다시 초기화 + 복구 줄");
    run_loops(3);
    // ⑧e 접촉한 바로 그 loop 뒤에 재부팅돼도 같다(접촉 표지가 다음 부팅으로 넘어가지 않는다)
    ++s_uid;
    make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
    card_place(&sc); run_loops(1); card_remove();
    logs_clear();
    g_versionReg = 0x00;
    hard_reset(false, 0);
    g_versionReg = 0x92;
    run_loops(1);
    CHECK(serial_count(ALIVE) == 1, "⑧e 접촉 loop 직후에 재부팅돼도 살아난 첫 loop 에 복구 줄(접촉 표지는 부팅을 넘지 않는다)");
    run_loops(3);
    // ⑧f 타입 변경 뒤 재시작(jmp 0 — RST 는 HIGH 그대로)은 Initialize 의 **소프트 리셋 갈래**로 초기화한다(시험의 boot/hard_reset 은 전원 켠 꼴)
    logs_clear();
    soft0 = g_softResets;
    GUARDED(setup());
    run_loops(2);
    CHECK(g_softResets == soft0 + 1 && serial_count(ALIVE) == 1 && rfid.IsConfigured(),
          "⑧f RST 가 HIGH 인 재시작(소프트 리셋 갈래)도 부팅 줄 1회 + 설정이 전부 들어간다");

    // ── ⑨ 부팅 때 읽기가 한 번 튀면 다시 읽어 정상 줄을 낸다(부팅 죽음 줄 = PC 음성을 튄 읽기 하나로 내지 않는다) ──
    logs_clear();
    sim_version_seq(0x00, 0x92);
    hard_reset(false, 2);
    CHECK(serial_count(ALIVE) == 1 && serial_count("WARNING") == 0 && serial_count("Firmware Version") == 1,
          "⑨(19차) 부팅 읽기가 한 번 튀어도 다시 읽은 값으로 정상 줄 1회");

    // ── ⑩ 다른 타입도 부팅 줄 1회(① 이 게이트웨이 · 여기서 소독기·서버·세척기) ──
    {
        const char others[] = {'D', 'S', 'W'};
        bool all = true;
        for (uint8_t i = 0; i < 3; ++i)
        {
            logs_clear();
            power_restore(); deviceOption.SetType(others[i]); hard_reset(false, 2);
            if (serial_count(ALIVE) != 1 || serial_count("WARNING") != 0) all = false;
        }
        CHECK(all, "⑩ 소독기·서버·세척기 타입도 부팅 때 버전 줄 1회(모든 타입)");
    }
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);

    // ── ⑪ 접촉 처리에는 줄이 끼지 않는다 ──
    logs_clear();
    CHECK(reads_tag() && serial_count("Firmware Version") == 0, "⑪ 접촉은 처리되고(세척 시작) 버전 줄은 끼지 않는다");

    // ── ⑫ loop 마다 번갈아 죽는 떨림(4초): 연속 두 번 죽은 적이 없으므로 줄도 재초기화도 없다 ──
    logs_clear();
    drop0 = g_fieldDrop; soft0 = g_softResets;
    {
        const uint32_t t0 = g_ms;
        uint8_t n = 0;
        while (g_ms - t0 < 4000UL && n < 200) { g_versionReg = (n & 1) ? 0x92 : 0x00; run_loops(1); ++n; }
        g_versionReg = 0x92; run_loops(3);
        tlog("  ⑫ 떨림 %u loop: 줄=%u 소프트리셋=%u\n", n, serial_count("Firmware Version"), (unsigned)(g_softResets - soft0));
    }
    CHECK(serial_count("Firmware Version") == 0 && g_softResets == soft0 && g_fieldDrop == drop0,
          "⑫(19차) loop 마다 번갈아 튀는 읽기에는 PC 줄도 재초기화도 없다(확정된 죽음이 없다)");

    // ── ⑬ 죽은 리더에 정상 읽기가 한 번 섞여도 PC 의 마지막 줄은 죽음으로 수렴한다 ──
    sim_advance_ms(6000);
    logs_clear();
    g_versionReg = 0x00;
    run_ms(1500);                                         // 죽음 줄
    sim_version_seq(0x92, 0x00);                          // 한 번 살아 보임 → 복구 줄
    run_loops(1);
    const int8_t mid = last_told();
    run_ms(7000);                                         // 다시 확정 + 5초 간격
    tlog("  ⑬ 죽음+튐: WARNING=%u 정상줄=%u 중간=%d 마지막=%d\n", serial_count("WARNING"), serial_count("= v2.0"), mid, last_told());
    CHECK(mid == 1 && last_told() == 0 && serial_count("WARNING") == 2,
          "⑬ 죽은 리더가 한 번 살아 보여 복구 줄이 나가도, 끝내 PC 의 마지막 줄은 죽음 줄이다(보낸 상태 기준으로 수렴)");

    // ── ⑬b 5초 간격은 **죽음 줄끼리** 잰다 — 복구 줄이 그 시계를 건드리면 "죽음 → 6초 → 복구 → 곧 다시 죽음" 의 둘째 죽음 줄이 5초 늦는다 ──
    g_versionReg = 0x92; run_loops(3);
    sim_advance_ms(6000);
    logs_clear();
    g_versionReg = 0x00; run_ms(6000);                    // 죽음 줄 1(0.8초쯤) · 그 뒤 5초 넘게 죽은 채
    g_versionReg = 0x92; run_loops(2);                    // 복구 줄
    g_versionReg = 0x00; run_ms(1500);                    // 곧 다시 죽음 — 확정(700ms)되면 바로 나가야 한다
    tlog("  ⑬b 죽음 6초 → 복구 → 재죽음 1.5초: WARNING=%u 정상줄=%u\n", serial_count("WARNING"), serial_count("= v2.0"));
    CHECK(serial_count("WARNING") == 2 && serial_count("= v2.0") == 1,
          "⑬b 간격은 앞 **죽음 줄**에서 잰다 — 복구 줄 직후의 재죽음도 앞 죽음 줄에서 5초가 지났으면 바로 알린다");

    // ── ⑭ 전원을 잃었다 돌아온 모듈(죽음을 봄) — 살아난 첫 loop 가 다시 초기화해 태그를 읽는다 ──
    sim_reader_power_cycle();                             // 다시 꽂음 — 칩은 리셋값(안테나 OFF)
    g_versionReg = 0x92;
    run_loops(2);
    CHECK(reads_tag(), "⑭ 전원을 잃었다 돌아온 리더가 태그를 다시 읽는다(종전 2.0~2.2.35: R-O 인데 재부팅 전까지 무반응)");

    // ── ⑮ 죽음을 못 본 순단(칩만 리셋) — 설정이 날아간 것이 두 loop 이어지면 조용히 다시 초기화(PC 줄 없음) ──
    logs_clear();
    soft0 = g_softResets;
    sim_reader_power_cycle();
    run_loops(1);
    const uint16_t softC1 = g_softResets - soft0;
    run_loops(2);
    const bool readC = reads_tag();
    tlog("  ⑮ 못 본 순단: 1loop 재초기화=%u 뒤=%u 읽힘=%d 줄=%u\n", softC1, (unsigned)(g_softResets - soft0), readC, serial_count("Firmware Version"));
    CHECK(softC1 == 0 && g_softResets == soft0 + 1 && readC && serial_count("Firmware Version") == 0,
          "⑮ 죽음 표본 없이 칩만 리셋돼도 두 loop 뒤 다시 초기화해 태그를 읽는다(한 loop 로는 안 한다 · PC 알림 없음)");

    // ── ⑯ 죽음 표본이 하나뿐인 순단(확정 못 한 죽음) — 살아난 loop 재초기화는 없지만 설정 판정이 받는다 ──
    sim_reader_power_cycle();
    sim_version_seq(0x00, 0x92);
    run_loops(4);
    CHECK(reads_tag(), "⑯ 죽음을 한 loop 만 본 순단도 설정 판정이 받아 태그를 읽는다");

    // ── ⑰ 살아난 첫 loop 의 재초기화가 반만 닿은 칩(안테나 비트만 켜짐) — 다음 두 loop 뒤 다시 초기화(18차는 여기서 굳었다) ──
    sim_advance_ms(6000);
    g_versionReg = 0x00; run_loops(3);                    // 확정된 죽음
    g_versionReg = 0x92; run_loops(1);                    // 살아난 첫 loop(재초기화)
    sim_reader_half_init();                               // …가 끝자락만 닿았다
    soft0 = g_softResets;
    run_loops(3);
    const bool readH = reads_tag();
    tlog("  ⑰ 반만 닿은 초기화: 그 뒤 재초기화=%u 읽힘=%d\n", (unsigned)(g_softResets - soft0), readH);
    CHECK(g_softResets == soft0 + 1 && readH,
          "⑰(19차) 살아난 첫 loop 의 초기화가 반만 닿아도 설정 판정이 다시 초기화해 태그를 읽는다(안테나 비트만 보면 영영 무반응)");
    // ⑰b 초기화의 첫 쓰기(TMode)만 빠진 칩 — 카드는 응답하지만 설정 판정이 알아 한 번 다시 초기화한다(판정의 TMode 항)
    soft0 = g_softResets;
    sim_reader_lose_first_write();
    run_loops(4);
    CHECK(g_softResets == soft0 + 1, "⑰b 초기화의 첫 쓰기가 빠진 칩도 다시 초기화한다(끝이 닿았으면 앞도 닿았는지 본다)");

    // ⑰d Initialize 의 쓰기 하나만 빠진 칩(일곱 레지스터 각각 · 8 = TMode 값만 틀림) — 설정 판정의 그 항이 알아 다시 초기화한다(항마다 표본 하나)
    for (uint8_t which = 1; which <= 8; ++which)
    {
        soft0 = g_softResets;
        sim_reader_drop_cfg(which);
        run_loops(3);
        const bool readD = reads_tag();
        char lb[72];
        snprintf(lb, sizeof(lb), "⑰d 초기화 레지스터 하나(%u)만 빠진 칩도 다시 초기화해 태그를 읽는다", which);
        CHECK(g_softResets == soft0 + 1 && readD, lb);
    }
    // ⑰c 부팅 쪽 형제: setup 의 초기화가 끝자락만 닿았는데 부팅 읽기는 살아 있는 값 — 죽음 표본이 없어도 설정 판정이 받는다
    //  (2.2.36 까지는 재초기화 조건 셋이 다 거짓이라 `R-O`·정상 줄인 채 재부팅 전까지 태그 무반응이었다 · VII-H)
    hard_reset(false, 0);
    sim_reader_half_init();
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);
    run_loops(3);
    CHECK(reads_tag(), "⑰c(19차) 부팅 순간 반만 닿은 초기화도 설정 판정이 다시 초기화해 태그를 읽는다");

    // ── ⑱ 설정 되읽기가 한 번씩 틀린 것으로는 아무것도 하지 않는다 — 사이에 맞게 읽히면 처음부터 다시 센다 ──
    soft0 = g_softResets;
    g_cfgUnreadable = true; run_loops(1); g_cfgUnreadable = false;
    run_loops(3);
    g_cfgUnreadable = true; run_loops(1); g_cfgUnreadable = false;   // 틀림 · 맞음 셋 · 틀림 — 이어진 둘이 아니다
    run_loops(3);
    CHECK(g_softResets == soft0, "⑱(19차) 설정 되읽기가 한 번씩 틀린 것으로는 재초기화하지 않는다(맞게 읽히면 다시 센다)");
    // ⑱b 틀린 읽기 둘 사이에 튄 죽음 읽기가 끼어도 둘째에서 고친다(죽음 읽기 하나는 설정 판정을 지우지 않는다)
    sim_reader_power_cycle();
    run_loops(1);                                         // 틀림 1
    sim_version_seq(0x00, 0x92);                          // 다음 loop 는 죽음 읽기(확정 아님) · 그 다음은 살아 있음
    soft0 = g_softResets;
    run_loops(1);
    const uint16_t softMid = g_softResets - soft0;
    run_loops(1);                                         // 틀림 2 → 다시 초기화
    const uint16_t softEnd = g_softResets - soft0;
    CHECK(softMid == 0 && softEnd == 1 && reads_tag(), "⑱b 틀린 읽기 둘 사이에 튄 죽음 읽기가 끼어도 둘째에서 다시 초기화한다");

    // ── ⑲ 설정이 **한 번도 맞게 읽히지 않는** 칩(되읽기가 안 되는 칩) — 부팅 뒤 세 번만 고쳐 보고 그만둔다 · 태그는 읽는다 ──
    g_cfgUnreadable = true;
    hard_reset(false, 0);
    soft0 = g_softResets;
    run_loops(3);
    const uint16_t softU3 = g_softResets - soft0;         // 둘째 loop 에 한 번(고친 뒤에는 다시 둘을 센다)
    run_loops(37);
    const uint16_t softU = g_softResets - soft0;
    const bool readU = reads_tag();
    run_loops(20);
    tlog("  ⑲ 되읽기 안 되는 칩: 재초기화=%u(3 loop) %u(40 loop) → %u(뒤 20 loop) 읽힘=%d\n", softU3, softU, (unsigned)(g_softResets - soft0), readU);
    CHECK(softU3 == 1 && softU == 3 && g_softResets == soft0 + 3 && readU,
          "⑲(19차) 설정이 한 번도 맞게 읽히지 않는 칩은 세 번만(두 loop 에 한 번) 다시 초기화하고 그만둔다 — 태그는 정상으로 읽는다");
    // ⑲b 그 칩이 죽었다 살아나면 살아난 첫 loop 의 한 번뿐 — 설정 판정으로는 더 고치지 않는다
    soft0 = g_softResets;
    g_versionReg = 0x00; run_loops(3);                    // 죽음 3 loop: 재초기화 2(둘째·셋째 loop)
    g_versionReg = 0x92; run_loops(20);                   // 살아난 첫 loop 1
    const bool readU2 = reads_tag();
    tlog("  ⑲b 죽었다 살아난 뒤 재초기화=%u 읽힘=%d\n", (unsigned)(g_softResets - soft0), readU2);
    CHECK(g_softResets == soft0 + 2 + 1 && readU2, "⑲b 그 칩은 죽었다 살아나도 살아난 첫 loop 의 한 번만 다시 초기화한다(태그는 읽는다)");
    // ⑲c 한 번이라도 맞게 읽힌 칩은 되읽기가 되는 칩이다 — 설정이 날아갈 때마다 제한 없이 고친다.
    //  (재검증 전의 19차 코드는 "연속 세 번" 예산이라, 맞게 읽힐 틈 없이 전원을 잃는 떨림에 닳아 `R-O` 인데 무반응으로 굳었다 — 2.2.36 에 없던 굳음)
    g_cfgUnreadable = false; run_loops(2);
    soft0 = g_softResets;
    for (uint8_t i = 0; i < 5; ++i) { sim_reader_power_cycle(); run_loops(2); }   // 맞게 읽힐 틈 없이 다섯 번 연달아 전원을 잃는다
    const uint16_t softF = g_softResets - soft0;
    const bool readF = reads_tag();
    tlog("  ⑲c 다섯 번 연달아 전원 잃음: 재초기화=%u 읽힘=%d\n", softF, readF);
    CHECK(softF == 5 && readF, "⑲c(19차 재검증) 맞게 읽힌 적이 있는 칩은 설정이 날아갈 때마다 고친다 — 떨리는 콘넥터에 굳지 않는다");
    // ⑲e 죽음이 256 loop 를 넘겨 이어져도 '확인된 죽음' 이 풀리지 않는다(계수 포화) — 살아나는 loop 에 다시 초기화
    g_versionReg = 0x00;
    for (uint16_t i = 0; i < 256; ++i) run_loops(1);
    soft0 = g_softResets;
    g_versionReg = 0x92; run_loops(1);
    CHECK(g_softResets == soft0 + 1, "⑲e 죽음이 256 loop 이어진 뒤 살아나도 그 loop 에 다시 초기화한다(계수가 한 바퀴 돌지 않는다)");
    run_loops(3);
    // ⑲d 다시 부팅하면 판정 상태가 처음으로 돌아간다 — 되읽기가 안 되는 칩이면 그 부팅에서 다시 세 번만
    g_cfgUnreadable = true;
    hard_reset(false, 0);
    soft0 = g_softResets;
    run_loops(40);
    CHECK(g_softResets == soft0 + 3, "⑲d 부팅마다 판정 상태(맞게 읽힌 적·고쳐 본 횟수)를 다시 놓는다");
    g_cfgUnreadable = false;
    hard_reset(false, 2);
    managerOption.SetData(mk, mn);
    recordOption.SetManagerDisposability(false);

    // ── ㉒ 상태 줄은 태그 접촉이 끝난 뒤 700ms 안에는 나가지 않는다(델파이·올눈은 덩이의 마지막 줄이 `Ok!`/`Sm!` 일 때만 저장한다) ──
    //  ㉒a 복구 줄: PC 에 죽음이 알려진 리더가 살아난 바로 그 loop 에 태그가 처리되면, 복구 줄은 다음 loop 가 아니라 700ms 뒤
    {
        sim_advance_ms(6000);
        logs_clear();
        g_versionReg = 0x00; run_ms(1500);                // 죽음 줄
        const bool told = serial_count("WARNING") == 1;
        ++s_uid;
        make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        g_versionReg = 0x92;
        sim_version_seq(0x00, 0x92);                      // 이 loop 의 머리 읽기는 아직 죽음 · 버스는 살아났다
        card_place(&sc);
        run_loops(1);                                     // 죽은 loop 의 재초기화가 닿고, 같은 loop 에 태그가 처리된다
        card_remove();
        const bool touched = get_process(sc).WashingStatus == 1;
        const uint32_t tEnd = g_ms;
        run_loops(1);
        const uint8_t soon = serial_count(ALIVE);
        while (serial_count(ALIVE) == 0 && g_ms - tEnd < 3000UL) run_loops(1);
        const uint32_t after = g_ms - tEnd;
        tlog("  ㉒a 죽음 알림=%d 접촉=%d 다음 loop 줄=%u 복구 줄 %lums 뒤\n", told, touched, soon, (unsigned long)after);
        CHECK(told && touched, "㉒a 전제: 죽음이 알려진 리더가 살아난 loop 에 태그가 처리됐다");
        CHECK(soon == 0 && after >= 700UL && after < 1000UL, "㉒a(19차 재검증) 복구 줄은 접촉이 끝난 뒤 700ms 를 넘겨 나간다(다음 loop 가 아니다)");
    }
    //  ㉒b 죽음 줄: 접촉 loop 의 머리 읽기가 튄 죽음이었고 곧 진짜로 죽어도, 죽음 줄은 접촉이 끝난 뒤 700ms 를 넘겨 나간다
    {
        run_loops(3);
        sim_advance_ms(6000);
        logs_clear();
        ++s_uid;
        make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        sim_version_seq(0x00, 0x00);                      // 접촉 loop 의 머리 읽기가 죽음(버스는 살아 있어 태그는 처리된다)
        card_place(&sc);
        const uint32_t tHead = g_ms;
        run_loops(1);
        card_remove();
        const bool touched = get_process(sc).WashingStatus == 1;
        const uint32_t tEnd = g_ms;
        g_versionReg = 0x00;                              // 이제 진짜 죽음
        while (serial_count("WARNING") == 0 && g_ms - tEnd < 3000UL) run_loops(1);
        const uint32_t after = g_ms - tEnd;
        tlog("  ㉒b 접촉=%d(걸린 %lums) 죽음 줄: 접촉 끝 뒤 %lums\n", touched, (unsigned long)(tEnd - tHead), (unsigned long)after);
        CHECK(touched && serial_count("WARNING") == 1 && after >= 700UL && after < 1000UL,
              "㉒b(19차 재검증) 죽음 줄도 접촉이 끝난 뒤 700ms 를 넘겨 나간다(튄 읽기에서 잰 700ms 가 아니다)");
        g_versionReg = 0x92; run_loops(3);
    }

    // ── ㉑ 처리한 태그를 아직 들고 있을 때 읽기가 한 번 튀어도 그 태그를 다시 처리하지 않는다(재초기화가 없으므로 전원이 안 끊긴다) ──
    //  18차까지는 튄 읽기 하나에 재초기화가 돌아(18차는 두 번) 정지가 풀린 같은 태그가 새 접촉으로 또 처리됐다
    //  (세척 = 재시작 · 2초 넘게 들고 있으면 종료 기록 · 서버는 덤프 뒤 Not W and D).
    {
        ++s_uid;
        make_tag(sc, s_uid, SCOPE_TYPE_TAG, s_uid, "SC", "SER");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
        card_place(&sc);
        run_loops(6);                                     // 세척 시작 → 무응답 3회 → 리더는 '떠났다' 로 본다(태그는 그대로 있다)
        const bool started = get_process(sc).Rewrite == 1 && rtc.HasAlarm(1);
        sim_advance_ms(3000);                             // 재시작 창(2초) 밖
        drop0 = g_fieldDrop;
        const uint16_t w0 = sc.writeCount;
        sim_version_seq(0x00, 0x92);                      // 읽기가 한 번 튐
        run_loops(10);
        const uint16_t dropG = g_fieldDrop - drop0, wG = sc.writeCount - w0;
        // 대조(5차 판정의 사실): 확인된 죽음(3 loop) 뒤 살아나면 재초기화가 전원을 끊어 같은 접촉이 또 처리된다 — 창 밖이라 종료
        g_versionReg = 0x00; run_loops(3);
        g_versionReg = 0x92; run_loops(6);
        const bool reprocessed = sc.writeCount != w0 && !rtc.HasAlarm(1);
        card_remove(); run_loops(4);
        tlog("  ㉑ 들고 있는 태그: 튄 읽기 → 필드끊김=%u 쓰기=%u · 확인된 죽음 뒤 재처리=%d\n", dropG, wG, reprocessed);
        CHECK(started && dropG == 0 && wG == 0, "㉑(19차) 들고 있는 태그 위에서 읽기가 한 번 튀어도 재초기화·재처리가 없다");
        CHECK(reprocessed, "㉑b 사실(5차 판정): 확인된 죽음 뒤 살아나면 재초기화가 들고 있던 태그를 새 접촉으로 처리한다(창 밖 = 종료)");
        run_loops(3);
    }

    // ── ⑳ 살아 있는 칩의 줄 글자(라이브러리와 같다) — 호환 칩·옛 판·모르는 판 ──
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
            tlog("  ⑳ 0x%02X: %d [%.34s]\n", k[i].v, ok, g_serialOut);
            if (!ok) all = false;
        }
        CHECK(all, "⑳ 살아 있는 칩은 실제 값 그대로(0x88 clone · 0x90 v0.0 · 0x91 v1.0 · 모르는 값 (unknown)) — WARNING 없음");
        g_versionReg = 0x92;
    }
    done();
    for (;;) {}
}
