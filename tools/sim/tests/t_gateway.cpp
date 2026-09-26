// 게이트웨이(G) — 올눈(ALLNuN) 조각 전송·SeePro 통짜 전송이 태그에 온전히 기록되는가.
#include "common.h"
#include "seepro_packet.h"

static SimCard a, b, c;

// 실기의 코어 루프처럼 loop() 뒤 수신 데이터가 있으면 serialEvent().
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
static void fresh_scope(SimCard &t, uint8_t uid, int no)
{
    make_tag(t, uid, SCOPE_TYPE_TAG, no, "SC", "SER");
    set_process(t, Process{0, 0, 0, 0, 0, false, 0, 0});
}
static bool block_cp949_ok(const uint8_t *p)   // 16바이트 안에서 한글 선두바이트가 짝 없이 끝나지 않는가
{
    for (uint8_t i = 0; i < 16 && p[i]; ++i)
    {
        if (p[i] >= 0x81)
        {
            if (i + 1 >= 16 || p[i + 1] == 0) return false;
            ++i;
        }
    }
    return true;
}

int main()
{
    rtc_set(DateTime(2026, 9, 23, 9, 0, 0));
    boot('G');

    // ── 올눈: G2·G3·G4·G5 를 따로, 250ms 간격(MainFormSo.pas UserInfoReaderSet) ──
    {
        const uint32_t t0 = g_ms + 50;
        const char g2[] = "G22026;9;23;4;10;30;0;";
        const char g3[] = "G312345;\xC8\xAB\xB1\xE6\xB5\xBF;;";          // 홍길동
        const char g4[] = "G4EGD;;;";
        const char g5[] = "G51995;04;19;F;";
        serial_queue(g2, sizeof(g2) - 1, t0);
        serial_queue(g3, sizeof(g3) - 1, t0 + 250);
        serial_queue(g4, sizeof(g4) - 1, t0 + 500);
        serial_queue(g5, sizeof(g5) - 1, t0 + 750);
        pump(4000);
        fresh_scope(a, 0x51, 51);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog_ldt("올눈 태그 검사일시", d);
        CHECK(ldt_eq(d, 2026, 9, 23, 10, 30, 0), "올눈 조각 전송 → 검사일시 기록");
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "12345", 6) == 0, "올눈 → 환자 키");
        CHECK(memcmp(a.data[SECTOR2_PATIENT_NAME], "\xC8\xAB\xB1\xE6\xB5\xBF", 7) == 0, "올눈 → 환자 이름");
        CHECK(serial_has("Sm!"), "올눈 → Sm! 응답");
    }

    // ── SeePro: 통짜 1회(15바이트 맞춤) — 한글 끝 글자가 반쪽으로 남지 않는다 ──
    {
        sim_advance_ms(60UL * 1000);
        serial_inject(kSeeProPacket, sizeof(kSeeProPacket) - 1);
        pump(3000);
        fresh_scope(b, 0x52, 52);
        logs_clear();
        touch(b);
        const LocalDateTime d = get_ldt(b, SECTOR1_GATEWAY);
        tlog_ldt("SeePro 태그 검사일시", d);
        CHECK(ldt_eq(d, 2026, 9, 23, 11, 0, 0), "SeePro 통짜 → 검사일시");
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "ABCDEFGHIJ12345", 16) == 0, "SeePro → 키 15바이트 전부");
        CHECK(block_cp949_ok(b.data[SECTOR2_PATIENT_NAME]) && b.data[SECTOR2_PATIENT_NAME][13] != 0,
              "SeePro → 이름 한글 7자 온전");
        CHECK(block_cp949_ok(b.data[SECTOR15_EXAMINATION_SUBJECT]) &&
              block_cp949_ok(b.data[SECTOR15_EXAMINATION_SUBJECT2]) &&
              b.data[SECTOR15_EXAMINATION_SUBJECT][13] != 0, "SeePro → 검사명 조각 한글 온전");
    }

    // ── 올눈식 16바이트 조각(StrLenCut)도 블록에 전부 담긴다 (09-23 사장님: 올눈 현장 있음) ──
    {
        sim_advance_ms(60UL * 1000);
        const char pkt[] = "G10000;G22026;9;23;3;12;0;0;G31;\xC8\xAB\xB1\xE6\xB5\xBF;G4"
                           "\xBB\xF3\xBA\xCE\xC0\xA7\xC0\xE5\xB0\xFC\xB3\xBB\xBD\xC3\xB0\xE6;;;G5;";   // 한글 8자 = 16바이트
        serial_inject(pkt, sizeof(pkt) - 1);
        pump(3000);
        fresh_scope(c, 0x53, 53);
        touch(c);
        CHECK(memcmp(c.data[SECTOR15_EXAMINATION_SUBJECT],
                     "\xBB\xF3\xBA\xCE\xC0\xA7\xC0\xE5\xB0\xFC\xB3\xBB\xBD\xC3\xB0\xE6", 16) == 0,
              "올눈 16바이트 조각 → 블록에 16바이트 전부");
        CHECK(block_cp949_ok(c.data[SECTOR15_EXAMINATION_SUBJECT]), "올눈 16바이트 조각 → 끝 글자 온전");
    }

    // ── [3차 G] 환자 키·이름(G3)도 16바이트 전부 담긴다(올눈은 자르지 않고 보낸다) ──
    {
        static SimCard e;
        sim_advance_ms(60UL * 1000);
        const char pkt[] = "G10000;G22026;9;23;3;14;0;0;G3ABCDEFGHIJKLMNOP;"
                           "\xB0\xA1\xB3\xAA\xB4\xD9\xB6\xF3\xB8\xB6\xB9\xD9\xBB\xE7\xBE\xC6;G4EGD;;;G5;";   // 키 16자, 이름 한글 8자
        serial_inject(pkt, sizeof(pkt) - 1);
        pump(3000);
        fresh_scope(e, 0x55, 55);
        touch(e);
        CHECK(memcmp(e.data[SECTOR2_PATIENT_KEY], "ABCDEFGHIJKLMNOP", 16) == 0, "3차G: 환자 키 16바이트 전부");
        CHECK(memcmp(e.data[SECTOR2_PATIENT_NAME], "\xB0\xA1\xB3\xAA\xB4\xD9\xB6\xF3\xB8\xB6\xB9\xD9\xBB\xE7\xBE\xC6", 16) == 0,
              "3차G: 환자 이름 16바이트 전부(끝 글자 온전)");
    }

    // ── [3차 G] Status=1 은 커밋 — 검사명 기록이 실패하면 "환자정보 있음" 이 서지 않는다 ──
    {
        static SimCard f;
        sim_advance_ms(60UL * 1000);
        fresh_scope(f, 0x56, 56);
        f.removeAfterOps = 22;                          // 게이트웨이 블록·환자 쓰기 뒤, 검사명 일괄 쓰기(21~26번째 작업) 도중 이탈
        logs_clear();
        touch(f);
        const Process p = get_process(f);
        tlog("  검사명 쓰기 중 이탈: Status=%u Sm!=%d\n", p.Status, serial_has("Sm!"));
        CHECK(p.Status == 0 && !serial_has("Sm!"), "3차G: 중간 실패 → Status 0 유지 · Sm! 없음");
    }
    // ── [3차 C] 검사명에 '{…}' 가 있어도(SeePro 는 무가공 송신) G 패킷을 JSON 으로 오판해 버리지 않는다 ──
    {
        static SimCard d;
        sim_advance_ms(60UL * 1000);
        const char pkt[] = "G10000;G22026;9;23;3;13;0;0;G3B0002;\xC8\xAB\xB1\xE6\xB5\xBF;G4{\xBC\xF6\xB8\xE9}EGD;;;G5;";   // {수면}EGD
        serial_inject(pkt, sizeof(pkt) - 1);
        pump(3000);
        fresh_scope(d, 0x54, 54);
        logs_clear();
        touch(d);
        tlog("  중괄호 검사명 뒤 태그 환자키 = %.8s\n", (const char *)d.data[SECTOR2_PATIENT_KEY]);
        CHECK(memcmp(d.data[SECTOR2_PATIENT_KEY], "B0002", 6) == 0, "3차C: '{' 가 든 G 도 환자정보로 채택(직전 환자 아님)");
        CHECK(memcmp(d.data[SECTOR15_EXAMINATION_SUBJECT], "{\xBC\xF6\xB8\xE9}EGD", 8) == 0, "3차C: 검사명 그대로 기록");
    }
    // ── Z2: 한 버퍼에 두 레코드가 병합될 때(수신 대기 1초보다 짧은 간격) 어느 환자가 기록되나 ──
    //    올눈 조각 모델(G1 없음)에서 종전엔 **첫(옛) 환자**가 다음 스코프에 기록됐다.
    {
        sim_advance_ms(60UL * 1000);
        const char pkt[] = "G22026;9;23;3;10;30;0;G3AAA001;NAMEA;;G4SUBJA;;;G51990;01;01;M;"
                           "G22026;9;23;3;12;15;0;G3BBB002;NAMEB;;G4SUBJB;;;G51991;02;02;F;";
        serial_inject(pkt, sizeof(pkt) - 1);
        pump(3000);
        fresh_scope(a, 0x57, 57);
        logs_clear();
        touch(a);
        const LocalDateTime d = get_ldt(a, SECTOR1_GATEWAY);
        tlog("  Z2 병합(G1 없음) → 환자키=%.8s 시각=%02u:%02u\n", (const char *)a.data[SECTOR2_PATIENT_KEY],
             d.Time.Hour, d.Time.Minute);
        CHECK(memcmp(a.data[SECTOR2_PATIENT_KEY], "BBB002", 6) == 0 && ldt_eq(d, 2026, 9, 23, 12, 15, 0) &&
              memcmp(a.data[SECTOR15_EXAMINATION_SUBJECT], "SUBJB", 5) == 0,
              "Z2 G1 없는 두 레코드 병합 → 마지막 환자 한 벌(첫 환자가 아니다)");
    }
    // ── Z2 P3-1: RX 링 511바이트 절단의 뒷동(머리 G1·G2 를 잃고 G3 에서 시작)은 쓰지 않는다 ──
    //    종전엔 환자는 맞고 검사일시만 0 으로 기록되고 성공음이 났다.
    {
        sim_advance_ms(60UL * 1000);
        const char whole[] = "G10000;G22026;9;23;3;15;0;0;G3OK0001;NAMEOK;;G4SUBJOK;;;G5;";
        serial_inject(whole, sizeof(whole) - 1);
        pump(3000);
        fresh_scope(b, 0x58, 58);
        touch(b);                                        // 온전한 패킷으로 한 번 기록(직전 환자 = OK0001)
        CHECK(memcmp(b.data[SECTOR2_PATIENT_KEY], "OK0001", 6) == 0, "Z2 전제: 온전한 패킷은 기록된다");
        sim_advance_ms(60UL * 1000);
        const char tail[] = "G3CUT001;NAMECUT;;G4SUBJCUT;;;G5;";   // 머리를 잃은 뒷동
        serial_inject(tail, sizeof(tail) - 1);
        pump(3000);
        fresh_scope(c, 0x59, 59);
        logs_clear();
        touch(c);
        tlog("  Z2 머리 잃은 조각 → 환자키=[%.8s] Status=%u Sm!=%d\n", (const char *)c.data[SECTOR2_PATIENT_KEY],
             get_process(c).Status, serial_has("Sm!"));
        CHECK(c.data[SECTOR2_PATIENT_KEY][0] == 0 && c.data[SECTOR2_PATIENT_NAME][0] == 0 &&
                  get_process(c).Status == 0 && !serial_has("Sm!"),
              "Z2 P3-1 머리(G1·G2)를 잃은 조각은 기록하지 않는다(Status 0 · Sm! 없음)");
    }
    // ── Z2 P2: G5 로 끝나지 않는(511 절단으로 꼬리를 잃은) 레코드는 쓰지 않는다 ──
    //    종전엔 환자를 받아들이고 검사항목을 통째로 빈칸으로 기록하며 성공음이 났다.
    {
        sim_advance_ms(60UL * 1000);
        const char cut[] = "G10000;G22026;9;23;3;16;0;0;G3TR0001;NAMETR;;G4SUB";   // G4 도중 절단
        serial_inject(cut, sizeof(cut) - 1);
        pump(3000);
        fresh_scope(a, 0x5A, 0x5A);
        logs_clear();
        touch(a);
        tlog("  Z2 꼬리 잃은 레코드 → 환자키=[%.8s] 이름=[%.8s] Status=%u Sm!=%d\n",
             (const char *)a.data[SECTOR2_PATIENT_KEY], (const char *)a.data[SECTOR2_PATIENT_NAME],
             get_process(a).Status, serial_has("Sm!"));
        CHECK(a.data[SECTOR2_PATIENT_KEY][0] == 0 && a.data[SECTOR2_PATIENT_NAME][0] == 0 &&
                  get_process(a).Status == 0 && !serial_has("Sm!"),
              "Z2 P2 G5 로 끝나지 않는 레코드는 기록하지 않는다(환자를 받아들이고 Sm! 내던 것)");
    }
    // ── Z2 P2 형제: 온전한 레코드 **뒤에** 잘린 레코드가 붙어 와도 아무것도 기록하지 않는다 ──
    //    G5 를 버퍼 전체에서 찾으면 앞 레코드의 G5 에 속아 잘린 뒤 레코드를 받아들인다.
    {
        sim_advance_ms(60UL * 1000);
        const char two[] = "G10000;G22026;9;23;3;17;0;0;G3FULL01;NAMEF;;G4SUBJF;;;G5;"
                           "G10000;G22026;9;23;3;18;0;0;G3CUT002;NAMEC;;G4SUB";
        serial_inject(two, sizeof(two) - 1);
        pump(3000);
        fresh_scope(b, 0x5B, 0x5B);
        logs_clear();
        touch(b);
        tlog("  Z2 온전+잘림 병합 → 환자키=[%.8s] Status=%u Sm!=%d\n", (const char *)b.data[SECTOR2_PATIENT_KEY],
             get_process(b).Status, serial_has("Sm!"));
        CHECK(b.data[SECTOR2_PATIENT_KEY][0] == 0 && get_process(b).Status == 0 && !serial_has("Sm!"),
              "Z2 P2 온전한 레코드 뒤 잘린 레코드 → 아무 환자도 기록하지 않는다(앞 레코드의 G5 에 속지 않는다)");
    }

    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
