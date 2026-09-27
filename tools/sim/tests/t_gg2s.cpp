// GG2 ② — 서버가 스코프를 **연속으로** 덤프할 때 앞 건이 뒤 건의 덤프에 섞이는가.
// 건마다 값을 전부 다르게 준다(스코프 번호·기기번호·시각·환자·검사항목) → 앞 건의 바이트가 뒤 건의
// 덤프 줄에 나타나는지를 **값으로** 가른다. 한 건 실패(읽기 오류 / 접촉 이탈) 뒤 정상 건 조합 포함.
#include "common.h"

static SimCard c1, c2;

// n 으로 모든 값을 갈라 둔다: 기기번호 = n, 시각 분 = n, 환자키 = "PKnn", 항목 = "SBnn"
static void done_scope(SimCard &t, uint8_t uid, int no, uint8_t dc)
{
    char id[8], ser[8], pk[8], pn[8], sb[8];
    snprintf(id, sizeof(id), "SC%02d", no);
    snprintf(ser, sizeof(ser), "S%03d", no);
    make_tag(t, uid, SCOPE_TYPE_TAG, no, id, ser);
    set_process(t, Process{1, dc, 1, 1, no, dc == 2, 0, 2});
    set_record(t, SECTOR2_WASHING_START, no, DateTime(2026, 9, 27, 9, no, 0));
    set_record(t, SECTOR3_WASHING_END, no, DateTime(2026, 9, 27, 9, no, 30));
    set_record(t, SECTOR5_DISINFECTION_START, no, DateTime(2026, 9, 27, 10, no, 0));
    set_record(t, SECTOR6_DISINFECTION_END, no, DateTime(2026, 9, 27, 10, no, 30));
    if (dc == 2)
    {
        set_record(t, SECTOR7_DISINFECTION_START, no, DateTime(2026, 9, 27, 11, no, 0));
        set_record(t, SECTOR8_DISINFECTION_END, no, DateTime(2026, 9, 27, 11, no, 30));
    }
    snprintf(pk, sizeof(pk), "PK%02d", no);
    snprintf(pn, sizeof(pn), "PN%02d", no);
    snprintf(sb, sizeof(sb), "SB%02d", no);
    put_block(t, SECTOR2_PATIENT_KEY, pk, 4);
    put_block(t, SECTOR2_PATIENT_NAME, pn, 4);
    put_block(t, SECTOR15_EXAMINATION_SUBJECT, sb, 4);
}

// "PK21" → "504B3231" 같은 대문자 hex (덤프 줄에서 찾는다)
static void hexof(char *out, const char *s)
{
    const char *d = "0123456789ABCDEF";
    uint8_t i = 0;
    for (; s[i]; ++i) { out[i * 2] = d[(uint8_t)s[i] >> 4]; out[i * 2 + 1] = d[(uint8_t)s[i] & 0xF]; }
    out[i * 2] = 0;
}
static bool has_text(const char *s)
{
    char h[24];
    hexof(h, s);
    return serial_has(h);
}

static void dump(SimCard &t)
{
    logs_clear();
    serial_inject("Z", 1);   // PC 의 PSOk 응답
    touch(t);
}

int main()
{
    rtc_set(DateTime(REL_YMD, 12, 0, 0));
    boot('S');
    serial_inject("Z", 1);
    GUARDED(serialEvent());
    run_loops(1);
    CHECK(lcd_has("connected"), "0 양성대조: 'Z' 인증");

    // ── #21 (DC=1) ──
    done_scope(c1, 0x51, 21, 1);
    dump(c1);
    tlog("  #21 Ok!=%d 1F1C=%d PK21=%d PK22=%d\n", serial_has("Ok!"), serial_has("1F1C"),
         has_text("PK21"), has_text("PK22"));
    CHECK(serial_has("Ok!") && has_text("PK21") && has_text("PN21") && has_text("SB21"),
          "A 양성대조: 첫 덤프가 그 스코프의 환자·항목을 담고 Ok! 로 끝난다");
    CHECK(!serial_has("1F1C") && !serial_has("2320"),
          "A DC=1 이면 2차 소독 줄(1F1C·2320)이 아예 없다");
    CHECK(get_process(c1).WashingStatus == 0, "A 덤프 뒤 태그 초기화");

    // ── #22 (DC=2 — 2차 소독 줄이 나간다) ──
    done_scope(c2, 0x52, 22, 2);
    dump(c2);
    tlog("  #22 1F1C=%d PK22=%d PK21=%d SB21=%d\n", serial_has("1F1C"), has_text("PK22"),
         has_text("PK21"), has_text("SB21"));
    CHECK(serial_has("Ok!") && serial_has("1F1C") && serial_has("2320"),
          "B DC=2 면 2차 소독 줄이 나간다");
    CHECK(has_text("PK22") && has_text("PN22") && has_text("SB22"),
          "B 둘째 덤프가 자기 환자·항목을 담는다");
    CHECK(!has_text("PK21") && !has_text("PN21") && !has_text("SB21"),
          "B 앞 스코프(#21)의 환자·항목 바이트가 둘째 덤프에 없다");

    // ── #23 (DC=1) 바로 뒤 — 앞 건의 조건부 줄(1F1C·2320)이 남는가 ──
    done_scope(c1, 0x53, 23, 1);
    dump(c1);
    tlog("  #23 1F1C=%d 2320=%d PK23=%d PK22=%d\n", serial_has("1F1C"), serial_has("2320"),
         has_text("PK23"), has_text("PK22"));
    CHECK(serial_has("Ok!") && has_text("PK23") && has_text("SB23"), "C 셋째 덤프가 자기 값을 담는다");
    CHECK(!serial_has("1F1C") && !serial_has("2320"),
          "C DC=2 덤프 바로 뒤의 DC=1 덤프에 2차 소독 줄이 남지 않는다");
    CHECK(!has_text("PK22") && !has_text("SB22"), "C 앞 스코프(#22)의 환자·항목이 없다");

    // ── #24 실패(블록 20 읽기 오류 2회) → Ok! 없음·태그 보존 ──
    done_scope(c2, 0x54, 24, 1);
    c2.readErrBlock = SECTOR5_DISINFECTION_START;
    c2.readErrTimes = 2;
    dump(c2);
    tlog("  #24 Ok!=%d ReadError=%d WS=%u\n", serial_has("Ok!"), lcd_has("Read Error"),
         get_process(c2).WashingStatus);
    CHECK(!serial_has("Ok!") && lcd_has("Read Error") && get_process(c2).WashingStatus == 1,
          "D 읽기 실패 덤프는 Ok! 없음·태그 보존");

    // ── #25 정상 (실패 바로 뒤) — 실패 잔재(mLegacyReadFailures)가 넘어오나 ──
    c2.readErrBlock = -1;
    c2.readErrTimes = 0;
    done_scope(c1, 0x55, 25, 1);
    dump(c1);
    tlog("  #25 Ok!=%d PK25=%d PK24=%d\n", serial_has("Ok!"), has_text("PK25"), has_text("PK24"));
    CHECK(serial_has("Ok!"), "E 실패한 건 바로 뒤의 정상 건은 Ok! 를 낸다(실패 잔재 없음)");
    CHECK(has_text("PK25") && !has_text("PK24"), "E 실패한 앞 건의 환자가 섞이지 않는다");
    CHECK(get_process(c1).WashingStatus == 0, "E 정상 건은 태그가 초기화된다");

    // ── #26 이른 거부(WS=0 → 'Not Washing') 를 실패와 정상 사이에 끼운다 ──
    //  거부는 mLegacyReadFailures 를 되돌리기(:403) 전에 나간다 — 그 잔재가 다음 건을 막는가
    done_scope(c2, 0x56, 26, 1);
    c2.readErrBlock = SECTOR5_DISINFECTION_START;
    c2.readErrTimes = 2;
    dump(c2);                                    // 실패 건(잔재를 남긴다)
    c2.readErrBlock = -1; c2.readErrTimes = 0;
    Process p26 = get_process(c2);
    p26.WashingStatus = 0;                       // 세척 표시 없는 스코프
    set_process(c2, p26);
    logs_clear(); serial_inject("Z", 1); touch(c2);
    tlog("  #26 NotWashing=%d\n", serial_has("Not Washing"));
    CHECK(serial_has("Not Washing") && !serial_has("Ok!"), "F 세척 표시 없는 스코프는 이른 거부");
    done_scope(c1, 0x57, 27, 1);
    dump(c1);
    tlog("  #27 Ok!=%d PK27=%d\n", serial_has("Ok!"), has_text("PK27"));
    CHECK(serial_has("Ok!") && has_text("PK27"),
          "F 실패 + 이른 거부를 거친 뒤에도 정상 건은 온전한 덤프를 낸다");

    // ── #28 접촉 이탈(카드 동작 20회 뒤 필드 이탈) → 다음 건 정상 ──
    done_scope(c2, 0x58, 28, 1);
    c2.removeAfterOps = 20;
    dump(c2);
    tlog("  #28 Ok!=%d WS=%u\n", serial_has("Ok!"), get_process(c2).WashingStatus);
    CHECK(!serial_has("Ok!"), "G 덤프 도중 접촉 이탈 → Ok! 를 내지 않는다");
    c2.removeAfterOps = 0;
    done_scope(c1, 0x59, 29, 1);
    dump(c1);
    tlog("  #29 Ok!=%d PK29=%d PK28=%d\n", serial_has("Ok!"), has_text("PK29"), has_text("PK28"));
    CHECK(serial_has("Ok!") && has_text("PK29") && !has_text("PK28"),
          "G 이탈한 앞 건 뒤의 정상 건이 온전하고 앞 환자가 섞이지 않는다");

    // ── ④ 누적 드리프트: 9건을 처리한 뒤 첫 건(#21)과 같은 스코프가 같은 덤프를 내는가 ──
    done_scope(c2, 0x5A, 21, 1);
    dump(c2);
    tlog("  L Ok!=%d PK21=%d 1F1C=%d 줄수표본(03021500)=%d\n", serial_has("Ok!"), has_text("PK21"),
         serial_has("1F1C"), serial_has("03021500"));
    CHECK(serial_has("Ok!") && has_text("PK21") && has_text("PN21") && has_text("SB21") &&
          !serial_has("1F1C") && !serial_has("2320") && serial_has("03021500"),
          "L 9건 뒤 #21 과 같은 스코프가 **첫 건과 같은** 덤프를 낸다(누적 드리프트 0)");

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
