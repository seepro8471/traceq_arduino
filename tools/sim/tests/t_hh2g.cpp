// HH2 ① 조작 x 결과 전수 지도 — 게이트웨이(G).
#include "hh2_sound.h"

static SimCard a, b;

static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('G');

    tlog("== G 게이트웨이 ==\n");

    // 환자정보 미수신 → 폴백 기록(기록은 됐다)
    fresh_scope(a, 0x31, 31);
    snd_begin(); touch(a);
    snd_show("G1 폴백 환자X");
    CHECK(snd_long2(), "G1 게이트웨이 환자정보 없음 = 길게 2회");
    const uint8_t g1dwN = dwell_count();
    const uint32_t g1dw = snd_dwell_of("No Patient In");

    // 폴백 쓰기 실패
    fresh_scope(b, 0x32, 32);
    b.failWriteAt = 1;
    snd_begin(); touch(b);
    snd_show("G2 폴백 쓰기실패");
    CHECK(snd_fail4(), "G2 폴백 쓰기 실패 = 짧게 4회");
    b.failWriteAt = 0;

    // 환자 패킷 수신
    {
        const char pkt[] = "G10007;G22026;9;28;3;10;0;0;G3A0001;HONG;G4EGD;;;G5;";
        serial_inject(pkt, sizeof(pkt) - 1);
        snd_begin();
        GUARDED(serialEvent());
        run_loops(1);
        snd_show("G3 패킷 수신");
    }
    const uint8_t g3p500 = buzz_count(500);
    const uint8_t g3dwN = dwell_count();

    // 스코프 + 환자정보 → 성공
    fresh_scope(a, 0x33, 33);
    snd_begin(); touch(a);
    snd_show("G4 기록 환자O");
    CHECK(snd_short1(), "G4 게이트웨이 정상 기록 = 짧게 1회");
    const uint32_t g4dw = snd_dwell_of("Scope :");

    // 환자정보 기록 쓰기 실패
    fresh_scope(b, 0x34, 34);
    b.failWriteAt = 1;
    snd_begin(); touch(b);
    snd_show("G5 기록 쓰기실패");
    CHECK(snd_fail4(), "G5 게이트웨이 기록 쓰기 실패 = 짧게 4회");
    b.failWriteAt = 0;

    // 읽기 실패
    fresh_scope(b, 0x35, 35);
    b.readErrBlock = SECTOR1_PROCESS; b.readErrTimes = 3;
    snd_begin(); touch(b);
    snd_show("G6 읽기실패");
    CHECK(snd_fail4(), "G6 게이트웨이 읽기 실패 = 짧게 4회");
    b.readErrBlock = -1; b.readErrTimes = 0;

    // 세척·소독 과정에 들어간 스코프 → No Complete 거부
    fresh_scope(b, 0x36, 36);
    set_process(b, Process{0, 0, 1, 0, 1, false, 0, 1});
    snd_begin(); touch(b);
    snd_show("G7 NoComplete");
    CHECK(snd_reject(), "G7 세척 들어간 스코프 = 거부음");
    const uint32_t g7dw = snd_dwell_of("No Complete");

    // 담당자 태그 · 클리어 태그 · 미지정 태그
    make_tag(a, 0x37, MANAGER_TYPE_TAG, 9, "ND09", "LEE");
    snd_begin(); touch(a);
    snd_show("G8 담당자태그");
    CHECK(snd_reject(), "G8 게이트웨이 + 담당자 태그 = 거부음");

    make_tag(a, 0x38, CLEAR_TYPE_TAG, 0, "", "");
    snd_begin(); touch(a);
    snd_show("G9 클리어태그");
    CHECK(snd_reject(), "G9 게이트웨이 + 클리어 태그 = 거부음");

    make_tag(a, 0x39, 0, 0, "", "");
    snd_begin(); touch(a);
    snd_show("G10 미지정태그");
    CHECK(snd_reject(), "G10 게이트웨이 + 종류 미지정 태그 = 거부음");

    // 잘린 패킷(G5 없음) — 받아들이지 않는다. 그래도 수신음은 난다.
    {
        const char pkt[] = "G10007;G22026;9;28;3;11;0;0;G3A0002;KIM;G4EGD;";
        serial_inject(pkt, sizeof(pkt) - 1);
        snd_begin();
        GUARDED(serialEvent());
        run_loops(1);
        snd_show("G11 잘린패킷");
    }
    const uint8_t g11p500 = buzz_count(500);

    tlog("== 측정값 ==\n");
    tlog("  폴백 글자수=%u · 수신 글자수=%u 500x%u · Scope글자 %lums · NoComplete %lums · 잘린패킷 500x%u\n",
         (unsigned)g1dwN, (unsigned)g3dwN, (unsigned)g3p500, (unsigned long)g4dw,
         (unsigned long)g7dw, (unsigned)g11p500);

    // ★13차 HH2 P3-7 봉합: 종전엔 `util_buzzer(400,2)` 만 불러 **글자가 없었다**(그 상태를 이 자리가 계약으로
    //  잠가 뒀다). 지금은 형제(세척·소독기)와 **같은 함수·같은 소리**이고 문구가 1.6초 뜬다.
    CHECK(g1dwN >= 1 && g1dw == 1600,
          "★게이트웨이 '환자정보 없음' 도 형제와 같이 `No Patient Info` 를 1.6초 띄운다(소리는 길게2 그대로)");
    CHECK(g3p500 == 1 && g3dwN == 0,
          "★게이트웨이 PC 수신 확인음도 500ms 1회이고 글자가 없다");
    CHECK(g11p500 == 1,
          "★잘린 패킷(버린다)도 온전한 패킷과 같은 수신음 — 소리로 유실을 알 수 없다");
    CHECK(g4dw >= 500 && g4dw <= 520, "정상 기록 문구 'Scope : NN' 은 500ms(무음) 동안 보인다");
    CHECK(g7dw == 1440, "거부 문구는 1440ms");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
