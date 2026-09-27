// HH2 ① 조작 x 결과 전수 지도 — 서버(S). PC 명령의 결과 소리까지.
#include "hh2_sound.h"

static SimCard a, b;

static void pump(uint32_t ms)
{
    const uint32_t end = g_ms + ms;
    while (g_ms < end)
    {
        GUARDED(loop());
        if (Serial.available() > 0) GUARDED(serialEvent());
        sim_advance_ms(20);
    }
}
static void raw(const char *s) { serial_inject(s, strlen(s)); GUARDED(serialEvent()); }
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static void done_scope(SimCard &t, uint8_t uid, int no)   // 세척·소독 다 끝난 태그(덤프 대상)
{
    fresh_scope(t, uid, no);
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, rel_date(9, 0, 0));
    set_record(t, SECTOR3_WASHING_END,   1, rel_date(9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, rel_date(9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END,   2, rel_date(9, 23, 0));
}

int main()
{
    rtc_set(rel_date(10, 0, 0));
    boot('S');

    tlog("== S 서버 ==\n");

    // 미인증 상태에서 스코프
    done_scope(a, 0x41, 41);
    snd_begin(); touch(a);
    snd_show("S1 미인증+스코프");
    CHECK(snd_reject(), "S1 미인증 서버 + 스코프 = 거부음");
    CHECK(!serial_has("Not Connected"), "S1 미인증 거부는 PC 로 문자열을 내지 않는다");

    // 인증
    snd_begin(); raw("Z"); run_loops(1);
    snd_show("S2 인증 Z");
    const uint8_t s2p500 = buzz_count(500);
    const uint32_t s2dw = snd_dwell_of("Program Start");

    // 정상 덤프
    done_scope(a, 0x42, 42);
    snd_begin(); serial_inject("Z", 1); touch(a);
    snd_show("S3 덤프 성공");
    const uint8_t s3p150 = buzz_count(150);
    const uint8_t s3dwN = dwell_count();
    CHECK(serial_has("Ok!"), "S3 덤프가 나갔다");

    // 세척·소독 없음
    fresh_scope(b, 0x43, 43);
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S4 NotWandD");
    CHECK(snd_reject(), "S4 세척·소독 없음 = 거부음");

    // 세척만 없음(소독 있음)
    fresh_scope(b, 0x44, 44);
    set_process(b, Process{1, 1, 0, 1, 1, false, 0, 2});
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S5 NotWashing");
    CHECK(snd_reject() && serial_has("Not Washing"), "S5 세척 없음 = 거부음");

    // 소독만 없음
    fresh_scope(b, 0x45, 45);
    set_process(b, Process{1, 0, 1, 0, 1, false, 0, 1});
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S6 NotDisinfect");
    CHECK(snd_reject() && serial_has("Not Disinfection"), "S6 소독 없음 = 거부음");

    // PC 무응답(PSOk 에 Z 가 안 온다) — ★앞 거부들이 삼키지 않은 'Z' 가 버퍼에 남아 있으므로 먼저 비운다
    while (Serial.available() > 0) (void)Serial.read();
    done_scope(b, 0x46, 46);
    snd_begin(); touch(b);
    snd_show("S7 PC무응답");
    CHECK(snd_reject() && serial_has("Not Connected"), "S7 PC 무응답 = 거부음");

    // 덤프 중 블록 읽기 실패 — Ok! 없이 실패음
    done_scope(b, 0x47, 47);
    b.readErrBlock = SECTOR5_DISINFECTION_START; b.readErrTimes = 6;
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S8 덤프 읽기실패");
    CHECK(snd_fail4() && !serial_has("Ok!"), "S8 덤프 중 읽기 실패 = 짧게 4회 · Ok! 없음");
    b.readErrBlock = -1; b.readErrTimes = 0;

    // 덤프 뒤 소거 실패
    done_scope(b, 0x48, 48);
    b.failWriteAt = b.writeCount + 4;
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S9 소거 실패");
    CHECK(snd_fail4(), "S9 덤프 뒤 소거 실패 = 짧게 4회");
    b.failWriteAt = 0;

    // 엉뚱한 종류의 태그
    make_tag(b, 0x49, MANAGER_TYPE_TAG, 9, "ND09", "LEE");
    snd_begin(); serial_inject("Z", 1); touch(b);
    snd_show("S10 담당자태그");
    CHECK(snd_reject(), "S10 서버 + 담당자 태그 = 거부음");

    // ── PC 명령의 결과 소리 ──
    tlog("== S PC 명령 ==\n");
    {
        char t[48];
        snprintf(t, sizeof(t), "T%u;%u;%u;3;11;22;33;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY);
        snd_begin(); raw(t);
        snd_show("S11 시각동기 성공");
    }
    const uint8_t s11p1000 = buzz_count(1000);
    const uint32_t s11dw = snd_dwell_of("updated");
    const bool s11set = (rtc.GetCurrentDateTime().minute() == 22);

    {
        // 달이 13 — 범위 밖이라 시계를 **안 바꾼다**
        char t[48];
        snprintf(t, sizeof(t), "T%u;13;%u;3;11;44;55;",
                 (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_DAY);
        snd_begin(); raw(t);
        snd_show("S12 시각동기 실패");
    }
    const uint8_t s12p1000 = buzz_count(1000);
    const uint8_t s12p100 = buzz_count(100);
    const uint32_t s12dw = snd_dwell_of("Invalid DateT");
    const bool s12kept = (rtc.GetCurrentDateTime().minute() == 22);

    {
        snd_begin(); raw("{\"cmd\":\"cfg_set_config\",\"patient_check\":true}");
        snd_show("S13 설정 저장");
    }
    const uint8_t s13p1000 = buzz_count(1000);

    {
        snd_begin(); raw("{\"cmd\":}");           // JSON 파싱 실패
        snd_show("S14 JSON 오류");
    }
    const uint8_t s14p1000 = buzz_count(1000);

    {
        snd_begin(); raw("{\"cmd\":\"nosuch\"}");  // 알 수 없는 명령
        snd_show("S15 모르는 명령");
    }
    const bool s15silent = snd_silent();

    // 발급 — 파싱 실패(즉시)
    snd_begin(); raw("S99;");
    snd_show("S16 발급 파싱실패");
    CHECK(snd_fail4(), "S16 발급 명령 파싱 실패 = 짧게 4회");

    // 발급 — 성공
    card_init_traceq(b, 0x4A);
    {
        Company co{};
        co.CompanyCode = TRACEQ_COMPANY_CODE;
        co.TagType = SCOPE_TYPE_TAG;
        put_block(b, SECTOR0_COMPANY, &co, sizeof(co));
    }
    snd_begin();
    card_place(&b);
    serial_inject("S555;SER555;", 12);
    pump(6000);
    card_remove(); run_loops(4);
    snd_show("S17 발급 성공");
    const uint8_t s17p500 = buzz_count(500), s17p50 = buzz_count(50);
    const uint32_t s17dw = snd_dwell_of("new tag");

    // 발급 — 태그 없음(4초 대기 뒤 실패)
    snd_begin();
    serial_inject("S556;SER556;", 12);
    pump(6000);
    snd_show("S18 발급 태그없음");
    CHECK(buzz_count(100) == 4, "S18 발급 대기 만료 = 짧게 4회(대기 비프 50ms 가 섞인다)");
    const uint8_t s18p50 = buzz_count(50);
    CHECK(s18p50 >= 2,
          "★발급 대기 비프는 1초마다 짧게 1회(=성공음과 같은 50ms) 다 — 규칙 표 밖의 소리");

    tlog("== 측정값 ==\n");
    tlog("  인증 500x%u %lums · 덤프 150x%u 글자수=%u\n",
         (unsigned)s2p500, (unsigned long)s2dw, (unsigned)s3p150, (unsigned)s3dwN);
    tlog("  시각동기: 성공 1000x%u %lums 반영=%d / 실패 1000x%u %lums 유지=%d\n",
         (unsigned)s11p1000, (unsigned long)s11dw, (int)s11set,
         (unsigned)s12p1000, (unsigned long)s12dw, (int)s12kept);
    tlog("  설정저장 1000x%u · JSON오류 1000x%u · 모르는명령 무음=%d\n",
         (unsigned)s13p1000, (unsigned)s14p1000, (int)s15silent);
    tlog("  발급성공 500x%u 50x%u %lums · 대기비프 50x%u\n",
         (unsigned)s17p500, (unsigned)s17p50, (unsigned long)s17dw, (unsigned)s18p50);

    CHECK(s2p500 == 1 && s17p500 == 1,
          "★인증 성공과 발급 성공이 같은 소리(500ms 1회) — 기기·단계가 달라도 같다");
    CHECK(s3p150 == 1 && s3dwN == 0,
          "★서버 덤프 성공음은 150ms 1회이고 **글자가 없다**(짧게1=50ms 와 다르다)");
    // ★13차 HH2 P2-1 봉합: 종전엔 성공(시계 바뀜)과 실패(시계 안 바뀜)가 같은 1000×1 이었고 **이 자리가 그
    //  상태를 계약으로 잠가 뒀다**. 지금은 실패가 실패음(100×4)이다 — 성공 쪽 1000×1 은 1.0 승계라 그대로 둔다.
    CHECK(s11p1000 == 1 && s12p1000 == 0 && s12p100 == 4 && s11set && s12kept,
          "★시각동기 성공은 안내음 1000×1 · 실패는 실패음 100×4 — 시계가 맞았는지 소리로 갈린다");
    CHECK(s11dw == 2000 && s12dw == 800,
          "★성공 문구는 2000ms · 실패 문구는 실패음 길이(100×4)만큼 800ms 보인다");
    CHECK(s13p1000 == 1 && s14p1000 == 1,
          "★설정 저장 성공과 JSON 파싱 실패도 같은 소리(1000ms 1회)다");
    CHECK(s15silent, "알 수 없는 명령은 무음(PC 만 안다)");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
