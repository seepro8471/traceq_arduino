// 하네스의 포화 관문(g_satAsk) 자체를 잠근다(19차) — LCD·시리얼·부저 로그 각각의 모서리.
//  selfcheck/t_guard.cpp 는 LCD 포화가 요약에 닿는지만 본다 — 시리얼·부저 쪽 표지 경로는 어느 시험도 지나지 않았다.
//  판정은 g_satAsk 를 직접 읽고, 끝에서 0 으로 되돌린다(이 시험이 일부러 올린 값이다).
#include "common.h"
#include <LiquidCrystal_I2C.h>

static LiquidCrystal_I2C L(0x27, 20, 4);
static void lcd_fill(uint16_t n) { for (uint16_t i = 0; i < n; ++i) L.write((uint8_t)'x'); }
static void ser_fill(uint16_t n) { for (uint16_t i = 0; i < n; ++i) Serial.write((uint8_t)'y'); }
static void pulses(uint8_t n, uint16_t ms)
{
    for (uint8_t i = 0; i < n; ++i) { digitalWrite(PIN_BUZZER, HIGH); sim_advance_ms(ms); digitalWrite(PIN_BUZZER, LOW); sim_advance_ms(5); }
}

int main()
{
    bool r; uint8_t c;
    const uint8_t s0 = g_satAsk;
    // ── LCD(1536B) ──
    logs_clear();
    L.write((uint8_t)'A'); L.write((uint8_t)'B'); L.write((uint8_t)'C');
    lcd_fill(1532);                                   // 1535자 = 꼭 찼고 버린 것은 없다
    r = lcd_has("NOPE");
    CHECK(!r && g_satAsk == s0, "L1 꼭 찼지만 버린 것이 없으면 '없다' 를 세지 않는다");
    L.write((uint8_t)'Z');                            // 버려진다
    r = lcd_has("ABC");
    CHECK(r && g_satAsk == s0, "L2 포화: '있다' 는 세지 않는다");
    r = lcd_has("NOPE");
    CHECK(!r && g_satAsk == s0 + 1, "L3 포화: '없다' 는 센다");
    r = serial_has("NOPE");
    CHECK(!r && g_satAsk == s0 + 1, "L4 LCD 포화는 시리얼 질문에 번지지 않는다");
    logs_clear();
    r = lcd_has("NOPE");
    CHECK(!r && g_satAsk == s0 + 1, "L5 logs_clear 뒤에는 세지 않는다");
    L.write((uint8_t)0); L.write((uint8_t)'Q');
    CHECK(lcd_has("Q"), "L6 NUL 이 쓰여도 그 뒤 글자가 보인다");
    // ── 시리얼(2048B) ──
    logs_clear();
    Serial.print("HELLO"); ser_fill(2042);            // 2047자 = 꼭 참
    r = serial_has("NOPE");
    CHECK(!r && g_satAsk == s0 + 1, "S1 꼭 찼지만 버린 것이 없으면 세지 않는다");
    Serial.write((uint8_t)'Q');
    r = serial_has("HELLO");
    CHECK(r && g_satAsk == s0 + 1, "S2 포화: '있다' 는 세지 않는다");
    r = serial_has("NOPE");
    CHECK(!r && g_satAsk == s0 + 2, "S3 포화: '없다' 는 센다");
    logs_clear();
    Serial.write((uint8_t)0); Serial.write((uint8_t)'Q');
    CHECK(serial_has("Q"), "S4 NUL 이 쓰여도 그 뒤 글자가 보인다");
    // ── 부저(64펄스) ──
    logs_clear();
    pulses(64, 100);
    c = buzz_count(100);
    CHECK(c == 64 && buzz_total() == 64 && g_satAsk == s0 + 2, "B1 64펄스: 정확하고 세지 않는다");
    pulses(1, 100);
    c = buzz_count(100);
    CHECK(c == 64 && g_satAsk == s0 + 3, "B2 65번째는 버려지고, 가득 찬 로그의 횟수 질문은 센다");
    c = buzz_total();
    CHECK(c == 64 && g_satAsk == s0 + 4, "B3 buzz_total 도 센다");
    buzz_clear();
    c = buzz_count(100);
    CHECK(c == 0 && g_satAsk == s0 + 4, "B4 buzz_clear 뒤에는 세지 않는다");
    pulses(70, 50); logs_clear();
    c = buzz_count(50);
    CHECK(c == 0 && g_satAsk == s0 + 4, "B5 logs_clear 가 부저 표지도 내린다");
    // ── 조용한 잘림(g_simCut) ──
    const uint8_t k0 = g_simCut;
    static char longScript[300];
    memset(longScript, 'l', 256); longScript[256] = 0;
    buttons_script(longScript);
    CHECK(g_simCut == k0, "C1 256자 스크립트는 온전하다");
    longScript[256] = 'l'; longScript[257] = 0;
    buttons_script(longScript);
    CHECK(g_simCut == k0 + 1, "C2 257자 스크립트는 잘리고 센다");
    buttons_script("");
    for (uint8_t i = 0; i < 9; ++i) serial_queue("x", 1, g_ms + 100000UL);
    CHECK(g_simCut == k0 + 2, "C3 serial_queue 아홉째 조각은 버려지고 센다");
    static char big2[601];
    memset(big2, 'x', 601);
    serial_inject(big2, 601);
    CHECK(g_simCut == k0 + 3, "C4 serial_inject 601B 는 잘리고 센다(가짜 수신 600B)");
    serial_inject("", 0);
    tlog("  satAsk=%u simCut=%u\n", (unsigned)g_satAsk, (unsigned)g_simCut);
    g_satAsk = 0; g_simCut = 0;                       // 이 시험이 일부러 올린 값 — 판정은 위 CHECK 다
    done();
    for (;;) {}
}
