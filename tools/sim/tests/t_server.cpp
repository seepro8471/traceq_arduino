// 서버(S) — F P2-2(덤프 중 읽기 실패 뒤 태그 초기화) 재현 + 레거시 덤프 회귀.
#include "common.h"

static SimCard ok, bad;

static Tag tag_of(const SimCard &c)
{
    Tag t{};
    memcpy(&t, c.data[SECTOR0_TAG], sizeof(t));
    return t;
}

static void done_scope(SimCard &t, uint8_t uid, int no)
{
    char id[8], ser[8];
    snprintf(id, sizeof(id), "SC%04d", no);
    snprintf(ser, sizeof(ser), "S%04d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, 1, 1, 1, 1, false, 0, 2});
    set_record(t, SECTOR2_WASHING_START, 1, DateTime(2026, 9, 22, 9, 0, 0));
    set_record(t, SECTOR3_WASHING_END, 1, DateTime(2026, 9, 22, 9, 4, 0));
    set_record(t, SECTOR5_DISINFECTION_START, 2, DateTime(2026, 9, 22, 9, 5, 0));
    set_record(t, SECTOR6_DISINFECTION_END, 2, DateTime(2026, 9, 22, 9, 23, 0));
}

int main()
{
    rtc_set(DateTime(2026, 9, 22, 10, 0, 0));
    boot('S');
    CHECK(serial_has("PSOk"), "부팅 PSOk");
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    CHECK(lcd_has("connected"), "Z 인증");

    // ── [회귀] 정상 덤프 → Ok! + 태그 초기화 ──
    {
        done_scope(ok, 0x31, 31);
        logs_clear();
        serial_inject("Z", 1);                          // PSOk 핸드셰이크 응답
        touch(ok);
        const Process p = get_process(ok);
        CHECK(serial_has("B;") && serial_has("W;") && serial_has("Ok!"), "회귀: 레거시 덤프 머리·끝");
        CHECK(serial_has("17140200EA07"), "회귀: 소독시작 블록 줄(1714…)");
        CHECK(p.WashingStatus == 0 && p.DisinfectionCount == 0, "회귀: 덤프 뒤 태그 초기화");
        // ★성공 경로의 최종 태그 상태는 1.4.1·2.2.8 과 같아야 한다 — 순서만 바꿨지 결과는 그대로.
        uint8_t proc[9]{};
        memcpy(proc, ok.data[SECTOR1_PROCESS], 9);
        bool procZero = true;
        for (uint8_t i = 0; i < 9; ++i) if (proc[i]) procZero = false;
        bool blocksZero = true;
        const uint8_t cleared[] = {SECTOR1_GATEWAY, SECTOR2_PATIENT_KEY, SECTOR2_PATIENT_NAME,
                                   SECTOR15_EXAMINATION_SUBJECT, SECTOR15_EXAMINATION_SUBJECT2,
                                   SECTOR15_EXAMINATION_SUBJECT3};
        for (uint8_t i = 0; i < sizeof(cleared); ++i)
            for (uint8_t j = 0; j < 16; ++j)
                if (ok.data[cleared[i]][j]) blocksZero = false;
        tlog("  최종 상태: Process 전부0=%d 소거블록 전부0=%d 세척시작 보존=%d\n",
             procZero, blocksZero, ok.data[SECTOR2_WASHING_START][0] != 0);
        CHECK(procZero && blocksZero, "회귀: 성공 경로 최종 태그 = 2.2.8 과 같은 결과(Process·소거 6블록 전부 0)");
        CHECK(ok.data[SECTOR2_WASHING_START][0] != 0, "회귀: 같은 섹터의 세척 시작 기록은 지우지 않는다");
    }
    // ── [F P2-2] 블록 20 을 두 번 못 읽어 0 으로 나간 덤프 뒤에는 태그를 지우지 않는다 ──
    {
        done_scope(bad, 0x32, 32);
        bad.readErrBlock = SECTOR5_DISINFECTION_START;
        bad.readErrTimes = 2;                           // Read + 재시도 모두 실패 → 0 덤프
        logs_clear();
        serial_inject("Z", 1);
        touch(bad);
        const Process p = get_process(bad);
        tlog("  읽기 실패 덤프 뒤 WS=%u DC=%u lcd=[%.60s]\n", p.WashingStatus, p.DisinfectionCount, g_lcdLog);
        CHECK(serial_has("171400000000000000000000000000000000;"), "F2-2: 실패 블록은 0 으로 덤프(와이어 불변)");
        CHECK(serial_has("Ok!"), "F2-2: 덤프 끝 Ok!(와이어 불변)");
        CHECK(lcd_has("Read Error"), "F2-2: LCD Read Error");
        CHECK(p.WashingStatus == 1 && p.DisinfectionCount == 1, "F2-2: 불완전 덤프 → 태그 보존(재스캔 복구)");
        // 재스캔하면 온전한 덤프 후 초기화
        logs_clear();
        serial_inject("Z", 1);
        touch(bad);
        CHECK(serial_has("17140200EA07") && get_process(bad).WashingStatus == 0, "F2-2: 재스캔 → 온전한 덤프 + 초기화");
    }
    tlog("  resets=%u\n", g_resetCount);
    // ── Z2 P3-2: 511 에서 끊긴 전문의 꼬리가 'S…' 로 시작해도 발급 명령으로 실행되지 않는다 ──
    //    종전엔 리더 위 스코프가 꼬리 속 값으로 덮이고 "new tag : scope" 성공 안내까지 났다.
    {
        sim_advance_ms(60UL * 1000);
        done_scope(ok, 0x71, 71);
        // 한 전문이 한도에서 끊긴다(511바이트) → 다음 읽기는 그 꼬리
        char burst[600];
        memset(burst, 'X', sizeof(burst));
        burst[0] = 'Z';                                  // 서버 raw 머리글자(인증 문자)
        serial_inject(burst, sizeof(burst));
        GUARDED(serialEvent());                          // 앞 511바이트
        logs_clear();
        GUARDED(serialEvent());                          // 꼬리 — 아래 S 명령이 여기 들어 있다고 보면 된다
        run_loops(1);
        card_place(&ok);                                 // 레거시 발급은 리더 위 태그에 쓴다
        run_loops(2);
        logs_clear();
        serial_inject("S99;SERX;", 9);
        GUARDED(serialEvent());                          // 꼬리 직후의 이 버퍼는 '꼬리' 로 버려지지 않아야 한다
        run_loops(2);
        tlog("  Z2 P3-2 꼬리 뒤 정상 발급 안내=%d 번호=%u\n", lcd_has("new tag"), tag_of(ok).Number);
        CHECK(lcd_has("new tag") && tag_of(ok).Number == 99,
              "Z2 P3-2 전제: 꼬리를 버린 뒤의 온전한 발급 명령은 그대로 동작한다");
        card_remove();
        run_loops(2);

        // 이제 진짜 꼬리가 'S…' 인 경우 — 끊긴 전문 바로 다음 버퍼
        sim_advance_ms(60UL * 1000);
        char burst2[520];
        memset(burst2, 'Y', sizeof(burst2));
        burst2[0] = 'Z';
        memcpy(burst2 + 511, "S88;SERY;", 9);            // 511 뒤(꼬리)가 발급 명령처럼 보인다
        card_place(&ok);
        run_loops(2);
        serial_inject(burst2, sizeof(burst2));
        GUARDED(serialEvent());                          // 앞 511
        logs_clear();
        GUARDED(serialEvent());                          // 꼬리 "S88;SERY;…"
        run_loops(1);
        tlog("  Z2 P3-2 꼬리 S88 → 안내=%d 번호=%u(99 유지)\n", lcd_has("new tag"), tag_of(ok).Number);
        CHECK(!lcd_has("new tag") && tag_of(ok).Number == 99,
              "Z2 P3-2 끊긴 전문의 꼬리는 발급 명령으로 실행되지 않는다(리더 위 태그가 덮이지 않는다)");
        card_remove();
    }
    done();
    for (;;) {}
}
