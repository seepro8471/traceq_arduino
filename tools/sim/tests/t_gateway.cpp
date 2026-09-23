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
    tlog("  resets=%u\n", g_resetCount);
    done();
    for (;;) {}
}
