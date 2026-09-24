// 서버(S) — F P2-2(덤프 중 읽기 실패 뒤 태그 초기화) 재현 + 레거시 덤프 회귀.
#include "common.h"

static SimCard ok, bad;

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
    done();
    for (;;) {}
}
