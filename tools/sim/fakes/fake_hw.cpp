// 하드웨어에 닿는 함수의 가짜 정의 — 선언은 전부 실제 헤더(코어·RTClib·LCD·EEPROM).
#include "harness.h"
#include <HardwareSerial.h>
#include "HardwareSerial_private.h"
#include <avr/eeprom.h>
#include <LiquidCrystal_I2C.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "TraceQ_Arduino/data/PinMap.hpp"

// ── 결과 로그 ──
char g_log[4096];
uint16_t g_pass, g_fail;
static uint16_t s_logLen;
void tlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int room = (int)sizeof(g_log) - (int)s_logLen - 1;
    if (room > 0)
    {
        const int n = vsnprintf(g_log + s_logLen, room, fmt, ap);
        if (n > 0) s_logLen += (n < room) ? n : room - 1;
    }
    va_end(ap);
}
// 실패 줄만 모으는 작은 버퍼 — g_log 가 넘쳐도 무엇이 실패했는지는 반드시 남는다.
char g_failLog[1024];
static uint16_t s_failLen;
void flog(const char *name)
{
    const int room = (int)sizeof(g_failLog) - (int)s_failLen - 1;
    if (room <= 1) return;
    const int n = snprintf(g_failLog + s_failLen, room, "FAIL %s\n", name);
    if (n > 0) s_failLen += (n < room) ? n : room - 1;
}

extern "C" void done() { asm volatile(""); }

// ── 시간 ──
volatile uint32_t g_ms;
void sim_advance_ms(uint32_t ms) { g_ms += ms; }
unsigned long millis() { return ++g_ms; }            // 대기 루프가 끝나도록 호출마다 1ms
unsigned long micros() { return g_ms * 1000UL; }
void delay(unsigned long ms) { g_ms += ms; }
void delayMicroseconds(unsigned int) {}

// ── 핀·부저·버튼 ──
static uint16_t s_buzzPulse[64];
static uint8_t  s_buzzN;
static uint32_t s_buzzOnAt;
static bool     s_buzzOn;
void buzz_clear() { s_buzzN = 0; }
uint8_t buzz_count(uint16_t pulseMs)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < s_buzzN; ++i) if (s_buzzPulse[i] == pulseMs) ++n;
    return n;
}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t val)
{
    if (pin != PIN_BUZZER) return;
    if (val == HIGH && !s_buzzOn) { s_buzzOn = true; s_buzzOnAt = g_ms; }
    else if (val == LOW && s_buzzOn)
    {
        s_buzzOn = false;
        if (s_buzzN < 64) s_buzzPulse[s_buzzN++] = (uint16_t)(g_ms - s_buzzOnAt);
    }
}
static char s_btn[256];
uint16_t g_minSP = 0xFFFF;
static uint16_t s_btnHead, s_btnLen;
static uint32_t s_btnIdleReads;
void buttons_script(const char *seq)
{
    const size_t n = strlen(seq);
    s_btnLen = (uint16_t)(n < sizeof(s_btn) ? n : sizeof(s_btn));
    
    memcpy(s_btn, seq, s_btnLen);
    s_btnHead = 0;
    s_btnIdleReads = 0;
}
int digitalRead(uint8_t pin)
{
    const uint16_t sp = SP;   // 버튼을 읽는 자리(메뉴 루프)의 스택 깊이 기록
    if (sp < g_minSP) g_minSP = sp;
    char want = 0;
    if (pin == PIN_SELECT_BUTTON) want = 'S';
    else if (pin == PIN_LEFT_BUTTON) want = 'L';
    else if (pin == PIN_RIGHT_BUTTON) want = 'R';
    else return HIGH;
    if (s_btnHead < s_btnLen && s_btn[s_btnHead] == want)
    {
        ++s_btnHead;
        s_btnIdleReads = 0;
        return LOW;
    }
    // 소문자('s','l','r') = 그 버튼이 "안 눌린 것"으로 한 번 읽힌다 — 눌렀다 뗐다 다시 누르는 순서를 표현한다.
    if (s_btnHead < s_btnLen && s_btn[s_btnHead] == static_cast<char>(want + 32))
    {
        ++s_btnHead;
        s_btnIdleReads = 0;
        return HIGH;
    }
    // 스크립트가 끝났는데 UI 가 계속 버튼을 기다리면 시험을 끊는다.
    if (s_btnHead >= s_btnLen && s_btnLen != 0 && ++s_btnIdleReads > 30000 && g_resetArmed)
        longjmp(g_resetJmp, 2);
    return HIGH;
}

// ── soft reset 가로채기 ── (제품 AvrUtil.cpp 의 정의는 빌드 때 다른 이름으로 돌려 둔다)
#include "TraceQ_Arduino/avr/AvrUtil.hpp"
jmp_buf g_resetJmp;
bool g_resetArmed;
uint16_t g_resetCount;
[[noreturn]] void util_soft_reset()
{
    ++g_resetCount;
    if (g_resetArmed) longjmp(g_resetJmp, 1);
    for (;;) {}
}

// ── 시리얼 ──
char g_serialOut[2048];
static uint16_t s_outLen;
static char s_in[600];
static uint16_t s_inHead, s_inLen;
// 도착 시각이 있는 입력 — 조각 k 는 s_chunkAt[k] 가 되어야 읽힌다(없으면 전부 즉시).
static uint16_t s_chunkEnd[8];
static uint32_t s_chunkAt[8];
static uint8_t  s_chunks;
void serial_inject(const char *s, size_t n)
{
    if (n > sizeof(s_in)) n = sizeof(s_in);
    memcpy(s_in, s, n);
    s_inHead = 0;
    s_inLen = (uint16_t)n;
    s_chunks = 0;
}
void serial_queue(const char *s, size_t n, uint32_t atMs)
{
    if (s_inHead >= s_inLen) { s_inHead = s_inLen = 0; s_chunks = 0; }
    if (s_chunks >= 8 || s_inLen + n > sizeof(s_in)) return;
    memcpy(s_in + s_inLen, s, n);
    s_inLen = (uint16_t)(s_inLen + n);
    s_chunkEnd[s_chunks] = s_inLen;
    s_chunkAt[s_chunks++] = atMs;
}
static uint16_t in_ready_end()
{
    if (s_chunks == 0) return s_inLen;
    uint16_t e = 0;
    for (uint8_t i = 0; i < s_chunks && s_chunkAt[i] <= g_ms; ++i) e = s_chunkEnd[i];
    return e;
}
void HardwareSerial::begin(unsigned long, byte) {}
void HardwareSerial::end() {}
int HardwareSerial::available() { const uint16_t e = in_ready_end(); return e > s_inHead ? e - s_inHead : 0; }
int HardwareSerial::peek() { return s_inHead < in_ready_end() ? (uint8_t)s_in[s_inHead] : -1; }
int HardwareSerial::read() { return s_inHead < in_ready_end() ? (uint8_t)s_in[s_inHead++] : -1; }
int HardwareSerial::availableForWrite() { return 64; }
void HardwareSerial::flush() {}
size_t HardwareSerial::write(uint8_t c)
{
    if (s_outLen < sizeof(g_serialOut) - 1) g_serialOut[s_outLen++] = (char)c;
    g_serialOut[s_outLen] = 0;
    return 1;
}
HardwareSerial Serial(&UBRR0H, &UBRR0L, &UCSR0A, &UCSR0B, &UCSR0C, &UDR0);

// ── EEPROM (RAM) ──
static uint8_t s_eeprom[4096];
uint8_t eeprom_read_byte(const uint8_t *p) { return s_eeprom[(uint16_t)(uintptr_t)p & 0x0FFF]; }
void eeprom_write_byte(uint8_t *p, uint8_t v) { s_eeprom[(uint16_t)(uintptr_t)p & 0x0FFF] = v; }

// ── LCD ──
char g_lcdLog[1536];
static uint16_t s_lcdLen;
static void lcd_put(char c)
{
    if (s_lcdLen < sizeof(g_lcdLog) - 1) g_lcdLog[s_lcdLen++] = c;
    g_lcdLog[s_lcdLen] = 0;
}
LiquidCrystal_I2C::LiquidCrystal_I2C(uint8_t a, uint8_t c, uint8_t r) : _Addr(a), _cols(c), _rows(r) {}
void LiquidCrystal_I2C::init() {}
void LiquidCrystal_I2C::backlight() {}
void LiquidCrystal_I2C::clear() { lcd_put('|'); }
void LiquidCrystal_I2C::setCursor(uint8_t, uint8_t) { lcd_put('|'); }
size_t LiquidCrystal_I2C::write(uint8_t c) { lcd_put((char)c); return 1; }

void logs_clear()
{
    s_lcdLen = 0; g_lcdLog[0] = 0;
    s_outLen = 0; g_serialOut[0] = 0;
    buzz_clear();
}
bool lcd_has(const char *s) { return strstr(g_lcdLog, s) != nullptr; }
bool serial_has(const char *s) { return strstr(g_serialOut, s) != nullptr; }

// ── RTC (DS3231) ──
static DateTime s_rtcBase(2026, 9, 22, 10, 0, 0);
static uint32_t s_rtcSetMs;
bool g_rtcLostPower;
void rtc_set(const DateTime &dt) { s_rtcBase = dt; s_rtcSetMs = g_ms; }
DateTime rtc_now_sim() { return s_rtcBase + TimeSpan((int32_t)((g_ms - s_rtcSetMs) / 1000UL)); }
bool RTC_DS3231::begin(TwoWire *) { return true; }
bool RTC_DS3231::lostPower() { return g_rtcLostPower; }
void RTC_DS3231::adjust(const DateTime &dt) { rtc_set(dt); g_rtcLostPower = false; }
DateTime RTC_DS3231::now() { return rtc_now_sim(); }
void RTC_DS3231::writeSqwPinMode(Ds3231SqwPinMode) {}
void RTC_DS3231::disableAlarm(uint8_t) {}
void RTC_DS3231::clearAlarm(uint8_t) {}

// ── SPI (RC522 는 가짜라 버스 불필요) ──
#include <SPI.h>
void SPIClass::begin() {}
SPIClass SPI;
