// HH2 ⑤ 안 잠긴 소리 판정을 잠근다 — HEAD 초록 · 변이 u1/u2 에서 빨강.
// 지금 소리를 "사실" 로 박는 것이므로, 소리를 바꾸려면 이 시험을 같이 고쳐야 한다(조용히 바뀌지 않는다).
#include "hh2_sound.h"

static SimCard mgr, a, b;

static void reboot_as(char type) { deviceOption.SetType(type); hard_reset(false); }
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void washed_scope(SimCard &t, uint8_t uid, int no, uint8_t status)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{status, 0, 1, 0, 1, false, 0, 0});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
}
static void done_scope(SimCard &t, uint8_t uid, int no)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END,   2, rel_date(9, 23, 0));
}
static void raw(const char *s) { serial_inject(s, strlen(s)); GUARDED(serialEvent()); }

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('D');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    // ── L-1 부팅 완료음 = 성공음과 같은 50ms 1회 (main.cpp) ──
    buzz_clear();
    hard_reset(false, 0);                 // setup() 만 — loop 은 돌리지 않는다
    tlog("  L-1 부팅음: 50=%u 100=%u\n", buzz_count(50), buzz_count(100));
    CHECK(buzz_count(50) == 1, "L-1 부팅 완료음 = 50ms 1회(main.cpp · 성공음과 같은 소리)");

    // ── L1 소독기에 여기서 안 되는 태그(종류 미지정) = 거부음 ──
    //  형제 넷 중 게이트웨이(main:383)·세척기(main:392)만 t_notify·t_a7 가 잠갔다.
    snd_begin(); touch(mgr);
    const uint8_t l0mgr = buzz_count(100);
    CHECK(l0mgr == 1, "L0 담당자 등록 성공음 = 100ms 1회(RecordProcessor::SaveManagerData)");

    make_tag(a, 0x11, 0, 0, "", "");
    snd_begin(); touch(a);
    tlog("  L1 D+미지정: 60=%u 600=%u 100=%u\n", buzz_count(60), buzz_count(600), buzz_count(100));
    CHECK(snd_reject() && serial_has("Invalid Tag Type"),
          "L1 소독기 + 여기서 안 되는 태그 = 거부음(실패음이 아니다 · main.cpp)");

    // ── L2 클리어 태그 처리음 · L3 최대 횟수 안내음 ──
    make_tag(a, 0x12, CLEAR_TYPE_TAG, 0, "", "");
    snd_begin(); touch(a);
    tlog("  L2 클리어: 500=%u\n", buzz_count(500));
    CHECK(buzz_count(500) == 1 && lcd_has("Clear"),
          "L2 클리어 태그(액교환 기록됨) = 소리가 난다(500ms 1회 · main.cpp)");

    touch(mgr);
    disinfectionOption.SetMaximumCount(1);
    disinfectionOption.SetCount(5);
    washed_scope(a, 0x13, 13, 1);
    snd_begin(); touch(a);
    tlog("  L3 MaxCount: 500=%u 50=%u\n", buzz_count(500), buzz_count(50));
    CHECK(buzz_count(500) == 1 && buzz_count(50) == 1 && lcd_has("MaxCount Over"),
          "L3 최대 횟수 초과 안내 = 500ms 1회 + 성공음(DisinfectionProcessor.cpp)");
    disinfectionOption.SetMaximumCount(0);

    // ═══ 서버 ═══
    reboot_as('S');

    // ── L4 PC 인증 안내음 ──
    snd_begin(); raw("Z"); run_loops(1);
    tlog("  L4 인증: 500=%u\n", buzz_count(500));
    CHECK(buzz_count(500) == 1 && lcd_has("Program Start"),
          "L4 PC 인증 = 500ms 1회(SerialProcessor.cpp)");

    // ── L5 덤프 완료음 ──
    done_scope(a, 0x21, 21);
    snd_begin(); serial_inject("Z", 1); touch(a);
    tlog("  L5 덤프완료: 150=%u Ok!=%d\n", buzz_count(150), serial_has("Ok!"));
    CHECK(buzz_count(150) == 1 && serial_has("Ok!"),
          "L5 서버 덤프 완료 = 150ms 1회(SerialProcessor.cpp)");

    // ── L6 세척만 없음 · L7 소독만 없음 = 거부음 ──
    fresh_scope(b, 0x22, 22);
    set_process(b, Process{1, 1, 0, 1, 1, false, 0, 2});
    snd_begin(); serial_inject("Z", 1); touch(b);
    CHECK(snd_reject() && serial_has("Not Washing"),
          "L6 서버 'Not Washing' = 거부음(SerialProcessor.cpp)");

    fresh_scope(b, 0x23, 23);
    set_process(b, Process{1, 0, 1, 0, 1, false, 0, 1});
    snd_begin(); serial_inject("Z", 1); touch(b);
    CHECK(snd_reject() && serial_has("Not Disinfection"),
          "L7 서버 'Not Disinfection' = 거부음(SerialProcessor.cpp)");

    // ── L8 PC 무응답 = 거부음 ──
    while (Serial.available() > 0) (void)Serial.read();
    done_scope(b, 0x24, 24);
    snd_begin(); touch(b);
    CHECK(snd_reject() && serial_has("Not Connected"),
          "L8 서버 PC 무응답 = 거부음(SerialProcessor.cpp)");

    // ── L9 서버 + 엉뚱한 종류의 태그 = 거부음 ──
    make_tag(b, 0x25, MANAGER_TYPE_TAG, 9, "ND09", "LEE");
    snd_begin(); serial_inject("Z", 1); touch(b);
    CHECK(snd_reject() && serial_has("Invalid Tag Type"),
          "L9 서버 + 엉뚱한 종류의 태그 = 거부음(main.cpp)");

    // ── L10 덤프 중 블록 읽기 실패 = 실패음(안내음이 아니다) ──
    //  CC2 P1-1 의 짝: `Ok!` 를 안 내는 것은 잠겼는데 **소리**는 안 잠겼다.
    while (Serial.available() > 0) (void)Serial.read();
    done_scope(b, 0x26, 26);
    b.readErrBlock = SECTOR5_DISINFECTION_START; b.readErrTimes = 6;
    snd_begin(); serial_inject("Z", 1); touch(b);
    tlog("  L10 덤프읽기실패: 100=%u 500=%u Ok!=%d\n", buzz_count(100), buzz_count(500), serial_has("Ok!"));
    CHECK(buzz_count(100) == 4 && buzz_count(500) == 0 && !serial_has("Ok!"),
          "L10 덤프 중 읽기 실패 = 실패음(짧게 4회) · 안내음이 아니다(SerialProcessor.cpp)");
    b.readErrBlock = -1; b.readErrTimes = 0;

    // ── L11 발급 성공 안내음 · L12 시각 동기 성공 안내음 ──
    snd_begin(); raw("{\"cmd\":\"cfg_set_config\",\"patient_check\":true}");
    tlog("  L11 설정저장: 1000=%u\n", buzz_count(1000));
    CHECK(buzz_count(1000) == 1 && lcd_has("updated"),
          "L11 설정 저장 = 1000ms 1회(SerialProcessor.cpp)");

    {
        char t[48];
        snprintf(t, sizeof(t), "T%u;%u;%u;3;11;22;33;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        snd_begin(); raw(t);
        tlog("  L12 시각동기 성공: 1000=%u 분=%u\n", buzz_count(1000),
             (unsigned)rtc.GetCurrentDateTime().minute());
        CHECK(buzz_count(1000) == 1 && lcd_has("updated") && rtc.GetCurrentDateTime().minute() == 22,
              "L12 레거시 시각 동기 성공 = 1000ms 1회(SerialProcessor.cpp)");
    }

    // ── L13 JSON 파싱 실패음 ──
    snd_begin(); raw("{\"cmd\":}");
    tlog("  L13 JSON오류: 1000=%u 100=%u\n", buzz_count(1000), buzz_count(100));
    // 15차 사장님 C3: 파싱 실패는 설정 저장 성공(1000×1)과 구별되는 실패음 — Invalid DateTime·Write Error 와 같은 100×4
    CHECK(buzz_count(100) == 4 && buzz_count(1000) == 0 && lcd_has("Invalid JSON"),
          "L13(15차 C3) JSON 파싱 실패 = 실패음 100ms 4회 + 글자 Invalid JSON(저장 성공 1000×1 과 다르다)");

    // ── L18 JSON 시각 동기 안내음(형제 SerialProcessor.cpp) ──
    {
        char j[128];
        rel_json_set_time(j, sizeof(j), 9, 30, 0);
        snd_begin(); raw(j);
        tlog("  L18 JSON 시각동기: 1000=%u\n", buzz_count(1000));
        CHECK(buzz_count(1000) == 1 && lcd_has("updated"),
              "L18 JSON 시각 동기 = 1000ms 1회(SerialProcessor.cpp)");
    }

    // ── L19 설정기 발급(cfg_new_tag) 성공 안내음 ──
    card_init_traceq(a, 0x31);
    snd_begin();
    card_place(&a);
    raw("{\"cmd\":\"cfg_new_tag\",\"type_id\":1}");
    card_remove(); run_loops(2);
    tlog("  L19 cfg_new_tag: 500=%u lcd=%d\n", buzz_count(500), lcd_has("tag created"));
    CHECK(buzz_count(500) == 1 && lcd_has("tag created"),
          "L19 설정기 발급 성공 = 500ms 1회(SerialProcessor.cpp)");

    // ── L14 메뉴에서 달력에 없는 날짜 = 실패음. 단 펄스가 40ms 로 형제(100ms)와 다르다 ──
    //  `UserInterface_device_option.cpp` 의 [5차 판정] 이 "소리 규칙(실패=짧게 4회)에 묶여 있다" 고
    //  적는데, 그 소리를 바꿔도 회귀가 초록이었다(변이 u1). 여기서 실측값으로 잠근다.
    rtc_set(DateTime(2026, 2, 28, 12, 0, 0));
    buttons_script("RRRRSRS" "RSRRS" "LLLLLLS");          // 260228 -> 260230
    snd_begin();
    GUARDED(ui.SetDeviceDate(rtc));
    tlog("  L14 Invalid Date: 40=%u 100=%u 글자수=%u\n",
         buzz_count(40), buzz_count(100), dwell_count());
    CHECK(lcd_has("Invalid Date") && buzz_count(40) == 4 && buzz_count(100) == 0,
          "L14 메뉴 날짜 범위 밖 = 40ms 4회(형제 실패음은 100ms 4회 · device_option.cpp)");
    const uint32_t l14dw = snd_dwell_of("Invalid Date");
    tlog("  L14 Invalid Date 글자 %lums\n", (unsigned long)l14dw);
    CHECK(l14dw >= 300 && l14dw <= 340,
          "L14 그 문구는 320ms 만 보인다(A2 P3-5 실측을 소리와 함께 잠근다)");

    // ── L15 메뉴 시간 범위 밖도 같은 소리(형제 device_option.cpp) ──
    //  content = "HHMMSS"(6자). 시 자리를 12 → 25 로 만들고 저장 자리(max=7)로 옮겨 저장한다.
    rtc_set(DateTime(2026, 9, 28, 12, 0, 0));
    buttons_script("SRS" "RS" "RRR" "S" "RRRRRR" "S");
    snd_begin();
    GUARDED(ui.SetDeviceTime(rtc));
    tlog("  L15 Invalid Time: 40=%u 100=%u lcd=%d 시=%u\n", buzz_count(40), buzz_count(100),
         lcd_has("Invalid Time"), (unsigned)rtc_now_sim().hour());
    CHECK(lcd_has("Invalid Time") && buzz_count(40) == 4 && buzz_count(100) == 0,
          "L15 메뉴 시간 범위 밖 = 40ms 4회(형제와 같은 규칙 · device_option.cpp)");

    // ── L16 기기 타입 변경 안내음(재시작 직전) ──
    deviceOption.SetType('S');
    hard_reset(false);
    snd_begin();
    raw("{\"cmd\":\"cfg_set_config\",\"device_type\":\"W\"}");
    tlog("  L16 타입변경: 1000=%u lcd=%d resets=%u\n", buzz_count(1000),
         lcd_has("updated, restart"), g_resetCount);
    CHECK(buzz_count(1000) == 1 && lcd_has("updated, restart"),
          "L16 기기 타입 변경 = 1000ms 1회 뒤 재시작(SerialProcessor.cpp)");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
