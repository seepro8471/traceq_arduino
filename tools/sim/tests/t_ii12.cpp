// II-H 제안 잠금 5 — HEAD 9379441 초록 · 되돌림 변이 빨강이어야 한다.
//  I) v2.2.22 레거시 T 의 `sep > 254` 보험(BB1 P3-4) — 코드 주석이 스스로 "이 한 줄만 지워도 빨강이 안 난다" 고
//     적은 자리. uint8 캐스팅으로 칸 경계가 256 에서 돌아 **앞쪽 바이트를 다시 읽어 그럴듯한 틀린 시각**이 되는
//     표본을 만들어 잠근다(첫 칸 260바이트 · 뒤 칸들이 앞쪽 2자리씩을 다시 읽는다).
//  J) 잔재 판정은 표지만(사장님 선택 1) — 검사일시는 Year≠0 만 보고 값은 안 본다. 월이 0 인 찢긴 검사일시도
//     완료 뒤 Status 0 이면 지운다. v2.2.29 의 `exam.isValid() && prev.isValid()` 관문은 2.2.32 에서 걷어냈다(되살리면 J 가 빨강).
//  K) v2.2.23 NeedsMoreBytes 의 "머리 없으면 기다리지 않는다" — t_gateway 의 걸린 시간 CHECK 는 'Z' 로 재는데
//     v2.2.24 부터 'Z' 는 IsGatewayFrame 이 먼저 거른다(헛초록). 머리(G1·G2) 없는 조각(G3…G5)으로 잰다.
#include "common.h"

static SimCard mgr, sc;
static unsigned char mk[ManagerOption::KEY_SIZE]  = {'M', 'G', 'R', '1'};
static unsigned char mn[ManagerOption::NAME_SIZE] = {'K', 'I', 'M'};
static char big[300];

static DateTime yday(uint8_t h, uint8_t m) { return DateTime(rel_date(h, m, 0).unixtime() - 86400UL); }
static void put2(int at, int v) { big[at] = (char)('0' + v / 10 % 10); big[at + 1] = (char)('0' + v % 10); }

int main()
{
    // ── I) 칸 경계 255 넘침 — 보험이 없으면 앞쪽 2자리씩을 다시 읽어 그럴듯한 시각이 된다 ──
    rtc_set(rel_date(10, 0, 0));
    boot('S');
    serial_inject("Z", 1); GUARDED(serialEvent()); run_loops(1);
    {
        memset(big, '0', sizeof(big));
        big[0] = 'T';
        const int y = TRACEQ_RELEASE_YEAR;
        big[1] = (char)('0' + y / 1000); big[2] = (char)('0' + y / 100 % 10);
        big[3] = (char)('0' + y / 10 % 10); big[4] = (char)('0' + y % 10);
        put2(6, TRACEQ_RELEASE_MONTH); put2(9, TRACEQ_RELEASE_DAY); put2(12, 3);   // 월 · 일 · 요일
        put2(15, 23); put2(18, 59); put2(21, 58);                                   // 시 · 분 · 초
        int p = 261;                                  // 첫 칸 = big[1..260] → 끝 (260&0xFF)=4 → "연도"
        for (int k = 0; k < 6; ++k) { big[p] = ';'; p += 3; }   // 뒤 여섯 칸은 2자리씩 — (at&0xFF) 가 6·9·12·15·18·21
        big[p] = ';';
        const size_t n = (size_t)p + 1;
        logs_clear(); buzz_clear();
        serial_inject(big, n); GUARDED(serialEvent()); run_loops(1);
        const DateTime now = rtc.GetCurrentDateTime();
        tlog("  I 270바이트 T → %02u:%02u:%02u Invalid=%d 100x%u\n", now.hour(), now.minute(), now.second(),
             (int)lcd_has("Invalid DateTime"), buzz_count(100));
        CHECK(now.hour() == 10 && lcd_has("Invalid DateTime"),
              "I 칸 경계가 255 를 넘는 T 는 거부한다 — 앞쪽 바이트를 다시 읽은 그럴듯한 시각(23:59)으로 맞추지 않는다");
    }

    // ── J) 검사일시 월이 0(찢긴 블록)이어도 표지 판정은 같다 — 완료 뒤 Status 0 이면 지운다 ──
    {
        rtc_set(rel_date(10, 0, 0));
        deviceOption.SetType('W');
        hard_reset(false, 2);
        managerOption.SetData(mk, mn);
        make_tag(sc, 0x61, SCOPE_TYPE_TAG, 61, "SC0061", "S0061");
        set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});   // 덤프 뒤(공정 0) — 잔재 판정이 도는 유일한 상태(14차)
        set_record(sc, SECTOR2_WASHING_START, 1, yday(9, 0));
        set_record(sc, SECTOR3_WASHING_END, 1, yday(9, 4));
        uint8_t g[16]{};
        const int dev = 7;
        memcpy(g, &dev, 2);
        LocalDateTime t = DefaultRtc::ToLocalDateTime(rel_date(8, 0, 0));
        t.Date.Month = 0;                              // 찢긴 검사일시(월 0)
        memcpy(g + 2, &t, sizeof(t));
        put_block(sc, SECTOR1_GATEWAY, g, 16);
        put_block(sc, SECTOR15_EXAMINATION_SUBJECT, "TORNEX01", 8);
        touch(sc, 2, 4);
        tlog("  J 월0 검사일시 → RW=%u 검사항목=%.8s\n", (unsigned)get_process(sc).Rewrite,
             (const char *)sc.data[SECTOR15_EXAMINATION_SUBJECT]);
        // [사장님 선택 1] 검사일시 값은 보지 않는다(표지만) — 찢긴 값(월 0)도 완료 뒤 Status 0 이면 지운다.
        CHECK(get_process(sc).Rewrite == 1 && sc.data[SECTOR15_EXAMINATION_SUBJECT][0] == 0,
              "J 월 0 인 찢긴 검사일시도 완료 뒤 Status 0 이면 지운다(값은 보지 않는다)");
    }

    // ── K) 머리(G1·G2) 없는 게이트웨이 조각은 이어 붙이기를 기다리지 않는다 ──
    {
        rtc_set(rel_date(11, 0, 0));
        deviceOption.SetType('G');
        hard_reset(false, 2);
        sim_advance_ms(60UL * 1000);
        const char frag[] = "G3PT0099;NAMEK;;G4SUBK;;;G5;";
        const uint32_t before = g_ms;
        serial_inject(frag, sizeof(frag) - 1);
        GUARDED(serialEvent());
        const uint32_t spent = g_ms - before;
        tlog("  K 머리 없는 조각 처리에 걸린 시간 = %lums\n", (unsigned long)spent);
        CHECK(spent < 2500UL, "K 머리 없는 조각(G3…G5)에는 이어 붙이기를 기다리지 않는다(폴링 정지 없음)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
