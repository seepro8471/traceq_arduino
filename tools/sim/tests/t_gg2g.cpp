// GG2 ① — 게이트웨이가 환자를 **연속으로** 받을 때 앞 환자의 값이 뒤 환자에게 새는가.
// 건마다 값을 전부 다르게 준다(키·이름·검사항목·본체번호·검사일시) — 빈 값으로 재면 "샌 것" 과 "원래 빈 것" 이
// 같은 모양이 된다. CHECK 는 "그래야 하는 것" 이므로 FAIL = 재현이다.
#include "common.h"

static SimCard sc;

static void packet(const char *s)
{
    serial_inject(s, strlen(s));
    GUARDED(serialEvent());
    run_loops(1);
}

static void fresh_scope(uint8_t uid, int no)
{
    make_tag(sc, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(sc, Process{0, 0, 0, 0, 0, false, 0, 0});
}

struct Seen
{
    int16_t gate;
    LocalDateTime dt;
    char key[17], name[17], s1[17], s2[17], s3[17];
};

static void cp(char *d, const uint8_t *s) { memcpy(d, s, 16); d[16] = 0; }

// 스코프를 한 번 대고 태그에 실제로 써진 값을 읽는다.
static Seen tap(uint8_t uid, int no)
{
    fresh_scope(uid, no);
    logs_clear();
    touch(sc);
    Seen r{};
    memcpy(&r.gate, sc.data[SECTOR1_GATEWAY], 2);
    r.dt = get_ldt(sc, SECTOR1_GATEWAY);
    cp(r.key, sc.data[SECTOR2_PATIENT_KEY]);
    cp(r.name, sc.data[SECTOR2_PATIENT_NAME]);
    cp(r.s1, sc.data[SECTOR15_EXAMINATION_SUBJECT]);
    cp(r.s2, sc.data[SECTOR15_EXAMINATION_SUBJECT2]);
    cp(r.s3, sc.data[SECTOR15_EXAMINATION_SUBJECT3]);
    tlog("  #%d gate=%d %02u:%02u:%02u key=[%s] name=[%s] s=[%s|%s|%s]\n", no, r.gate,
         r.dt.Time.Hour, r.dt.Time.Minute, r.dt.Time.Second, r.key, r.name, r.s1, r.s2, r.s3);
    return r;
}

static bool eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

// g_serialOut 안에서 needle 이 몇 번 나오나 — 와이어 줄 수 드리프트를 센다
static int count_str(const char *needle)
{
    const size_t n = strlen(needle);
    int c = 0;
    for (const char *p = g_serialOut; *p; ++p)
        if (strncmp(p, needle, n) == 0) ++c;
    return c;
}
// 게이트웨이 전문 = 정확히 4줄(S; · 0302… · 0704… · Sm!)
static bool frame_ok(int no)
{
    char want[16];
    snprintf(want, sizeof(want), "0302%02X%02X", no & 0xFF, (no >> 8) & 0xFF);
    return count_str("\r\n") == 4 && count_str("S;") == 1 && count_str("Sm!") == 1 &&
           count_str("0704") == 1 && serial_has(want);
}

int main()
{
    rtc_set(DateTime(REL_YMD, 9, 0, 0));
    boot('G');

    // ── A: 검사항목 3개 ──
    packet("G10011;G22026;9;27;4;09;01;01;G3KEYA1;NAMEA1;;G4SUBA1;SUBA2;SUBA3;G5;");
    Seen a = tap(0x41, 41);
    CHECK(eq(a.key, "KEYA1") && eq(a.name, "NAMEA1") && eq(a.s1, "SUBA1") && eq(a.s2, "SUBA2") &&
          eq(a.s3, "SUBA3") && a.gate == 11 && a.dt.Time.Minute == 1,
          "A 양성대조: 첫 환자 한 벌이 그대로 기록된다");
    tlog("  A 와이어 줄수=%d S;=%d Sm!=%d\n", count_str("\r\n"), count_str("S;"), count_str("Sm!"));
    CHECK(frame_ok(41), "A 와이어 = 정확히 4줄(S;·0302·0704·Sm!)");

    // ── B: 검사항목이 2개로 줄었다 — A 의 3번째가 남는가 ──
    packet("G10012;G22026;9;27;4;09;02;02;G3KEYB1;NAMEB1;;G4SUBB1;SUBB2;;G5;");
    Seen b = tap(0x42, 42);
    CHECK(eq(b.key, "KEYB1") && eq(b.name, "NAMEB1") && eq(b.s1, "SUBB1") && eq(b.s2, "SUBB2"),
          "B 두 항목 환자의 앞 두 칸이 옳다");
    CHECK(b.s3[0] == 0, "B 항목이 3->2 로 줄면 3번째 칸이 빈다(A 의 SUBA3 가 남지 않는다)");
    CHECK(b.gate == 12 && b.dt.Time.Minute == 2, "B 본체번호·검사일시가 B 것이다");

    // ── C: 이름이 빈 칸 · 항목 1개 ──
    packet("G10013;G22026;9;27;4;09;03;03;G3KEYC1;;G4SUBC1;;;G5;");
    Seen c = tap(0x43, 43);
    CHECK(eq(c.key, "KEYC1"), "C 키가 C 것이다");
    CHECK(c.name[0] == 0, "C 이름이 빈 칸이면 빈 칸으로 기록된다(NAMEB1 이 남지 않는다)");
    CHECK(eq(c.s1, "SUBC1") && c.s2[0] == 0 && c.s3[0] == 0,
          "C 항목 1개 — 2·3번째 칸이 빈다(SUBB2 가 남지 않는다)");

    // ── D: 짧은 패킷(검사항목 칸이 전부 빈 칸) ──
    //  ★내 첫 판정식 오류(기록): G4 마커 자체를 뺀 패킷으로 쟀더니 폴백('환자정보 없음')이 났다 —
    //   `GatewaySerialEvent:95` 는 G3..G4 를 한 쌍으로 요구한다(D 갈래 문제없음 8, 설계). 그래서 G4 는 둔다.
    packet("G10014;G22026;9;27;4;09;04;04;G3KEYD1;NAMED1;;G4;;;G5;");
    Seen d = tap(0x44, 44);
    CHECK(eq(d.key, "KEYD1") && eq(d.name, "NAMED1"), "D 짧은 패킷의 환자가 옳다");
    CHECK(d.s1[0] == 0 && d.s2[0] == 0 && d.s3[0] == 0,
          "D 검사항목이 전부 빈 칸이면 세 칸 모두 빈다(SUBC1 이 남지 않는다)");
    CHECK(d.gate == 14 && d.dt.Time.Minute == 4, "D 본체번호·검사일시가 D 것이다");

    // ── E: 같은 환자를 두 번 ──
    packet("G10014;G22026;9;27;4;09;04;04;G3KEYD1;NAMED1;;G4;;;G5;");
    Seen e = tap(0x45, 45);
    CHECK(eq(e.key, d.key) && eq(e.name, d.name) && e.s1[0] == 0 && e.gate == d.gate &&
          e.dt.Time.Minute == 4,
          "E 같은 환자를 두 번 보내면 두 번째도 같다");

    // ── F: 이름만 바뀐 환자(키 동일) ──
    packet("G10014;G22026;9;27;4;09;05;05;G3KEYD1;NAMEF1;;G4SUBF1;;;G5;");
    Seen f = tap(0x46, 46);
    CHECK(eq(f.key, "KEYD1") && eq(f.name, "NAMEF1") && eq(f.s1, "SUBF1"),
          "F 이름만 바뀐 환자가 새 이름으로 기록된다");

    // ── G: 패킷 없이 스코프만 한 번 더(한 검사에 스코프 둘) ──
    Seen g = tap(0x47, 47);
    CHECK(eq(g.key, f.key) && eq(g.name, f.name) && eq(g.s1, f.s1) && g.gate == f.gate &&
          g.dt.Time.Second == f.dt.Time.Second,
          "G 패킷 없이 둘째 스코프를 대면 같은 환자가 기록된다(수명 = 재론 금지 판정)");

    // ── H: 잘린 패킷(G5 없음) 뒤에 온전한 패킷 — 실패한 건이 다음 건을 오염시키나 ──
    packet("G10015;G22026;9;27;4;09;06;06;G3KEYH1;NAMEH1;;G4SUBH1");
    Seen h = tap(0x48, 48);
    tlog("  H(잘린 패킷 뒤) NotPatient=%d Sm!=%d Status=%u\n", serial_has("Not Patient Info"),
         serial_has("Sm!"), get_process(sc).Status);
    CHECK(h.key[0] == 0 && h.name[0] == 0,
          "H 잘린 패킷은 환자정보를 세우지 않고 앞 환자(F)도 남기지 않는다");
    // ★값만 보면 안 된다 — 키·이름 칸이 비어도 Status=1 로 커밋되면 세척기의 '환자정보 없음' 경고가
    //  죽고 PC 는 빈 등록번호를 저장한다. 그래서 **커밋 표지와 와이어**로도 가른다.
    CHECK(get_process(sc).Status == 0 && serial_has("Not Patient Info") && !serial_has("Sm!"),
          "H 잘린 패킷 뒤 스코프는 '환자정보 없음'으로 기록된다(Status=0 · Sm! 없음)");
    packet("G10016;G22026;9;27;4;09;07;07;G3KEYI1;NAMEI1;;G4SUBI1;SUBI2;;G5;");
    Seen i = tap(0x49, 49);
    CHECK(eq(i.key, "KEYI1") && eq(i.name, "NAMEI1") && eq(i.s1, "SUBI1") && eq(i.s2, "SUBI2") &&
          i.s3[0] == 0 && i.gate == 16,
          "H 잘린 패킷 다음의 온전한 패킷은 온전히 기록된다");

    // ── J: G1 값이 빈 칸("G1;") — 본체번호가 앞 환자 것으로 남는가 ──
    packet("G1;G22026;9;27;4;09;08;08;G3KEYJ1;NAMEJ1;;G4SUBJ1;;;G5;");
    Seen j = tap(0x4A, 50);
    tlog("  J G1 빈 칸 -> gate=%d (앞 환자 I 는 16)\n", j.gate);
    CHECK(eq(j.key, "KEYJ1") && eq(j.name, "NAMEJ1"), "J 환자 한 벌은 J 것이다");
    CHECK(j.gate == i.gate,
          "J(기록) G1 값이 빈 칸이면 본체번호는 **앞 환자 것**이 남는다 — 초록이면 그 잔재가 있다");

    // ── K: G1 값이 아예 없는 형식("G1G2…" — 세척관리) ──
    packet("G1G22026;9;27;4;09;09;09;G3KEYK1;NAMEK1;;G4SUBK1;;;G5;");
    Seen k = tap(0x4B, 51);
    tlog("  K G1G2 형식 -> gate=%d\n", k.gate);
    CHECK(eq(k.key, "KEYK1") && eq(k.name, "NAMEK1"), "K 세척관리 형식의 환자 한 벌이 옳다");

    // ── L: ④ 누적 드리프트 — 11건 뒤 A 와 같은 모양의 패킷이 같은 결과를 내는가 ──
    packet("G10011;G22026;9;27;4;09;01;01;G3KEYA1;NAMEA1;;G4SUBA1;SUBA2;SUBA3;G5;");
    Seen l = tap(0x4C, 52);
    CHECK(eq(l.key, a.key) && eq(l.name, a.name) && eq(l.s1, a.s1) && eq(l.s2, a.s2) &&
          eq(l.s3, a.s3) && l.gate == a.gate && l.dt.Time.Second == a.dt.Time.Second,
          "L 11건 뒤 같은 패킷이 첫 건과 **완전히 같은** 결과를 낸다(누적 드리프트 0)");
    tlog("  L 와이어 줄수=%d\n", count_str("\r\n"));
    CHECK(frame_ok(52), "L 11건 뒤에도 와이어가 정확히 4줄(줄 수 드리프트 0)");

    // ── M: 읽기 실패 건이 앞 건의 스코프 번호를 3행에 남기나(화면 잔재) ──
    packet("G10011;G22026;9;27;4;09;11;11;G3KEYM1;NAMEM1;;G4SUBM1;;;G5;");
    fresh_scope(0x4D, 53);
    sc.readErrBlock = SECTOR0_TAG;
    sc.readErrTimes = 4;
    logs_clear();
    touch(sc);
    tlog("  M 3행=[%s] ReadError=%d\n", lcd_row(3), lcd_has("Read Error"));
    CHECK(lcd_has("Read Error"), "M 태그 번호를 못 읽으면 Read Error");
    // 12차 봉합: 못 읽으면 3행을 비운다 — 'Read Error' 옆에 앞 건(52)의 번호가 남아 남의 스코프로 보였다.
    CHECK(strncmp(lcd_row(3), "00052", 5) != 0,
          "M 읽기 실패 건의 3행에 앞 건(52)의 스코프 번호가 남지 않는다");
    sc.readErrBlock = -1; sc.readErrTimes = 0;

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
