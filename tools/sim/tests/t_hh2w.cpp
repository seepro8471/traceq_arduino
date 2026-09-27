// HH2 ① 조작 x 결과 전수 지도 — 세척기(W) · 소독기(D).
// 소리(펄스 길이 x 횟수)와 2행 글자가 남은 시간을 **실측**한다. CHECK 는 사장님 소리 규칙(09-23)으로 판정한다.
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

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('W');
    make_tag(mgr, 0x01, MANAGER_TYPE_TAG, 7, "ND01456", "KIMJH");

    tlog("== W 세척기 ==\n");
    // 담당자 등록 — 성공
    snd_begin(); touch(mgr);
    snd_show("W1 담당자등록");
    const bool w1short50 = snd_short1();
    const uint8_t w1p100 = buzz_count(100);
    const uint32_t w1dw = dwell_count() ? dwell_ms(0) : 0;

    // 담당자 태그를 한 번 더 — 같은 소리(재등록)
    snd_begin(); touch(mgr);
    snd_show("W2 담당자재등록");

    // 스코프 시작(환자정보 있음 Status=1)
    fresh_scope(a, 0x11, 11);
    set_process(a, Process{1, 0, 0, 0, 0, false, 0, 0});
    snd_begin(); touch(a);
    snd_show("W3 시작 환자O");
    CHECK(snd_short1(), "W3 세척 시작(환자정보 있음) = 짧게 1회");

    // 스코프 종료(2초 창 밖)
    sim_advance_ms(60UL * 1000);
    snd_begin(); touch(a);
    snd_show("W4 종료 환자O");
    CHECK(snd_short1(), "W4 세척 종료(환자정보 있음) = 짧게 1회");

    // 스코프 시작(환자정보 없음 Status=0)
    fresh_scope(b, 0x12, 12);
    snd_begin(); touch(b);
    snd_show("W5 시작 환자X");
    CHECK(snd_long2(), "W5 세척 시작(환자정보 없음) = 길게 2회");
    const uint32_t w5dw = snd_dwell_of("No Patient");

    // 더블터치(2초 안 재접촉) = 시작 재실행
    snd_begin(); touch(b);
    snd_show("W6 더블터치");

    // 쓰기 실패
    fresh_scope(a, 0x13, 13);
    a.failWriteAt = 1;
    snd_begin(); touch(a);
    snd_show("W7 쓰기실패");
    CHECK(snd_fail4(), "W7 세척 쓰기 실패 = 짧게 4회");
    const uint32_t w7dw = snd_dwell_of("Write Error");
    a.failWriteAt = 0;

    // 읽기 실패
    fresh_scope(a, 0x14, 14);
    a.readErrBlock = SECTOR1_TAG_SERIAL; a.readErrTimes = 5;
    snd_begin(); touch(a);
    snd_show("W8 읽기실패");
    CHECK(snd_fail4(), "W8 세척 읽기 실패 = 짧게 4회");
    a.readErrBlock = -1; a.readErrTimes = 0;

    // 클리어 태그 — 세척기에서는 처리 불가
    make_tag(a, 0x15, CLEAR_TYPE_TAG, 0, "", "");
    snd_begin(); touch(a);
    snd_show("W9 클리어태그");
    CHECK(snd_reject(), "W9 세척기 + 클리어 태그 = 거부음");

    // 종류 미지정(0) 태그 — 발급 중간 상태
    make_tag(a, 0x16, 0, 0, "", "");
    snd_begin(); touch(a);
    snd_show("W10 미지정태그");
    CHECK(snd_reject(), "W10 세척기 + 종류 미지정 태그 = 거부음");
    const uint32_t w10dw = snd_dwell_of("Invalid Tag");

    // 담당자 미등록 거부 — EEPROM 을 비우고 다시
    eeprom_factory(); hard_reset(false); reboot_as('W');
    fresh_scope(a, 0x17, 17);
    snd_begin(); touch(a);
    snd_show("W11 담당자미등록");
    CHECK(snd_reject(), "W11 담당자 미등록 = 거부음");

    // ═══ D 소독기 ═══
    tlog("== D 소독기 ==\n");
    reboot_as('D');
    snd_begin(); touch(mgr);
    snd_show("D1 담당자등록");

    washed_scope(a, 0x21, 21, 1);
    snd_begin(); touch(a);
    snd_show("D2 시작 환자O");
    CHECK(snd_short1(), "D2 소독 시작(환자정보 있음) = 짧게 1회");

    sim_advance_ms(5UL * 60 * 1000);
    snd_begin(); touch(a);
    snd_show("D3 종료 환자O");
    CHECK(snd_short1(), "D3 소독 종료(환자정보 있음) = 짧게 1회");

    washed_scope(b, 0x22, 22, 0);
    snd_begin(); touch(b);
    snd_show("D4 시작 환자X");
    CHECK(snd_long2(), "D4 소독 시작(환자정보 없음) = 길게 2회");

    // 세척 안 한 스코프
    fresh_scope(a, 0x23, 23);
    snd_begin(); touch(a);
    snd_show("D5 세척안함");
    CHECK(snd_reject(), "D5 세척 안 한 스코프 = 거부음");
    const uint32_t d5dw = snd_dwell_of("No Washing");

    // 클리어 태그 = 액교환(처리 성공)
    make_tag(a, 0x24, CLEAR_TYPE_TAG, 0, "", "");
    snd_begin(); touch(a);
    snd_show("D6 클리어태그");
    const bool d6long1 = (buzz_count(500) == 1);
    const uint32_t d6dw = snd_dwell_of("Clear");

    // 종류 미지정 태그
    make_tag(a, 0x25, 0, 0, "", "");
    snd_begin(); touch(a);
    snd_show("D7 미지정태그");
    CHECK(snd_reject(), "D7 소독기 + 종류 미지정 태그 = 거부음");

    // MaxCount Over — 최대 횟수를 1 로 두고 이미 1회 이상
    disinfectionOption.SetMaximumCount(1);
    disinfectionOption.SetCount(5);
    washed_scope(a, 0x26, 26, 1);
    snd_begin(); touch(a);
    snd_show("D8 MaxCountOver");
    const uint8_t d8p500 = buzz_count(500), d8p50 = buzz_count(50);
    const uint32_t d8dwMax = snd_dwell_of("MaxCount");
    disinfectionOption.SetMaximumCount(0);

    // 쓰기 실패 · 읽기 실패
    washed_scope(a, 0x27, 27, 1);
    a.failWriteAt = 1;
    snd_begin(); touch(a);
    snd_show("D9 쓰기실패");
    CHECK(snd_fail4(), "D9 소독 쓰기 실패 = 짧게 4회");
    a.failWriteAt = 0;

    washed_scope(a, 0x28, 28, 1);
    a.readErrBlock = SECTOR1_TAG_SERIAL; a.readErrTimes = 5;
    snd_begin(); touch(a);
    snd_show("D10 읽기실패");
    CHECK(snd_fail4(), "D10 소독 읽기 실패 = 짧게 4회");
    a.readErrBlock = -1; a.readErrTimes = 0;

    // 알람 발화 — 슬롯 2
    washed_scope(a, 0x29, 29, 1);
    touch(a);                                     // 소독 시작 → 알람 설정
    snd_begin();
    sim_advance_ms(40UL * 60 * 1000);             // 소독 시간(18분) 지남
    run_loops(2);
    snd_show("D11 알람발화");
    const uint8_t d11p300 = buzz_count(300), d11p600 = buzz_count(600);

    tlog("== 측정값 ==\n");
    tlog("  담당자 등록음: 50x%u 100x%u  글자 %lums\n", (unsigned)(w1short50 ? 1 : 0),
         (unsigned)w1p100, (unsigned long)w1dw);
    tlog("  글자 남은시간: 환자X %lums · WriteErr %lums · 거부 %lums · NoWashing %lums · Clear %lums · MaxCnt %lums\n",
         (unsigned long)w5dw, (unsigned long)w7dw, (unsigned long)w10dw, (unsigned long)d5dw,
         (unsigned long)d6dw, (unsigned long)d8dwMax);
    tlog("  알람 발화: 300x%u 600x%u\n", (unsigned)d11p300, (unsigned)d11p600);

    // ★규칙 대조 — 어긋난 자리를 사실로 박는다
    CHECK(w1p100 == 1 && !w1short50,
          "★담당자 등록 성공음은 100ms 1회다(다른 성공음 50ms 와 길이가 다르다 · 1.0 승계)");
    CHECK(w1dw > 0 && w1dw <= 220,
          "★담당자 등록 글자는 200ms 뿐 — 사람이 ID 를 읽을 수 없다");
    CHECK(d6long1 && buzz_count(50) == 0 && d8p500 == 1,
          "★클리어 태그 성공과 MaxCount Over 경고가 같은 소리(500ms 1회)다");
    CHECK(d8p50 == 1, "MaxCount Over 뒤에 정상 성공음(50ms)이 이어진다");
    CHECK(d11p300 == 4 && d11p600 == 0,
          "★알람 발화 펄스는 300ms 4회다(600ms 가 아니다 — 600ms 는 거부음 펄스)");
    CHECK(w5dw == 1600 && w7dw == 800 && w10dw == 1440,
          "글자 남는 시간: 환자정보없음 1600 · 실패 800 · 거부 1440ms");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
