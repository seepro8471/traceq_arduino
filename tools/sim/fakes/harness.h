#pragma once
// 펌웨어 시뮬레이터 시험 장치 — 하드웨어에 닿는 함수만 가짜, 제품 코드는 실물.
#include <Arduino.h>
#include <RTClib.h>
#include <setjmp.h>

// ── 카드 모델 (ISO14443-3 상태 + MIFARE Classic 인증) ──
enum SimCardState : uint8_t { CARD_OFF, CARD_IDLE, CARD_READY, CARD_ACTIVE, CARD_AUTH, CARD_HALT };

struct SimCard
{
    uint8_t uid[4];
    uint8_t sak;
    uint8_t data[64][16];
    uint8_t keyA[16][6];
    uint8_t keyB[16][6];
    bool    keyBAuth[16];     // KEY_B 로 인증 가능한가(공장 트레일러는 불가)
    // 고장 주입
    int16_t failAuthAt;       // n번째 인증 시도를 실패시킨다(카드 → IDLE). 0=없음
    int16_t failWriteAt;      // n번째 쓰기를 실패시킨다(카드 → IDLE). 0=없음
    int16_t removeAfterOps;   // 인증·읽기·쓰기 n회 뒤 필드 이탈. 0=없음
    int16_t readErrBlock;     // 이 블록 읽기를 리더 쪽 오류로(카드 상태 유지). -1=없음
    uint8_t readErrTimes;     // 위 오류 횟수
    bool    loseHalt;         // 다음 HaltA 프레임 유실
    // 계수
    uint16_t authCount, writeCount, readCount, opCount;
};

extern SimCard *g_card;            // 필드 위 카드(없으면 nullptr)
extern SimCardState g_cardState;
extern bool g_readerCrypto;

void card_init_traceq(SimCard &c, uint8_t uidLast);   // TraceQ 키 태그(데이터 0)
void card_init_foreign(SimCard &c, uint8_t uidLast);  // 키가 다른 MIFARE 카드
void card_place(SimCard *c);                          // 필드 진입(전원 인가 → IDLE)
void card_remove();                                   // 필드 이탈

// ── 시간 ──
extern volatile uint32_t g_ms;
void sim_advance_ms(uint32_t ms);
void rtc_set(const DateTime &dt);
DateTime rtc_now_sim();
extern bool g_rtcLostPower;

// ── LCD·시리얼·부저 기록 ──
extern char g_lcdLog[1536];
extern char g_serialOut[2048];
void logs_clear();
bool lcd_has(const char *s);
bool serial_has(const char *s);
void serial_inject(const char *s, size_t n);
void serial_queue(const char *s, size_t n, uint32_t atMs);   // g_ms 가 atMs 가 되어야 읽히는 조각
uint8_t buzz_count(uint16_t pulseMs);   // 길이가 pulseMs 인 부저 펄스 수
void buzz_clear();

// ── 버튼 스크립트 (LOW=눌림) ──
void buttons_script(const char *seq);   // 'L','S','R' 한 글자 = 한 번 누름
extern uint16_t g_minSP;                // 버튼 읽는 자리에서 본 최저 SP

// ── soft reset 가로채기 ──
extern jmp_buf g_resetJmp;
extern bool g_resetArmed;
extern uint16_t g_resetCount;

// ── 결과 ──
extern char g_log[4096];
extern uint16_t g_pass, g_fail;
void tlog(const char *fmt, ...);
#define CHECK(cond, name) do { if (cond) { ++g_pass; tlog("PASS %s\n", name); } \
                               else { ++g_fail; tlog("FAIL %s\n", name); } } while (0)
extern "C" void done() __attribute__((noinline));
