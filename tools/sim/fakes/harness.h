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
    int16_t nackBlock;        // 이 블록 쓰기만 NACK — **카드는 살아 있다**(실물의 쓰기 거부·ACK 유실).
                              //   가정 ③ 및 "한 블록만 실패해도 나머지는 성공" 을 표현한다. -1=없음
    uint8_t nackBlockSkip;    // 그 블록의 처음 n회 쓰기는 통과(검증 재기록 뒤부터 실패시킬 때)
    int16_t removeAfterOps;   // 인증·읽기·쓰기 n회 뒤 필드 이탈. 0=없음
    int16_t readErrBlock;     // 이 블록 읽기를 리더 쪽 오류로(카드 상태 유지). -1=없음
    uint8_t readErrTimes;     // 위 오류 횟수
    uint8_t readErrSkip;      // 그 블록의 처음 n회 읽기는 통과시킨 뒤 오류(예: 쓰기 확인 읽기만 실패)
    bool    loseHalt;         // 다음 HaltA 프레임 유실
    // 계수
    uint16_t authCount, writeCount, readCount, opCount;
};

extern SimCard *g_card;            // 필드 위 카드(없으면 nullptr)
extern SimCardState g_cardState;
extern bool g_readerCrypto;
extern uint8_t  g_versionReg;      // 0x00·0xFF = 죽은 리더(제품이 Reinitialize 를 탄다)
extern uint16_t g_fieldDrop;       // 리더 소프트 리셋으로 카드가 전원을 잃은 횟수
void sim_field_drop();

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
void buttons_script(const char *seq);
void buttons_at(char btn, uint32_t atMs);   // g_ms 가 atMs 에 이르면 그 버튼('S','L','R')이 한 번 눌린다
extern uint32_t g_btnIdleLimit;        // 스크립트 소진 뒤 이만큼 더 읽으면 시험을 끊는다(기본 30000)   // 'L','S','R' = 한 번 누름 · 소문자 'l','s','r' = 그 버튼을 한 번 안 눌린 것으로 읽음
extern uint16_t g_minSP;                // 버튼 읽는 자리에서 본 최저 SP

// ── soft reset 가로채기 ──
extern jmp_buf g_resetJmp;
extern bool g_resetArmed;
extern uint16_t g_resetCount;

// ── 결과 ──
extern char g_log[4096];
// ★실패 줄은 **따로** 모은다 — g_log 가 가득 차면 PASS 에 밀려 FAIL 이름이 통째로 사라진다(진단 불가).
extern char g_failLog[1024];
extern uint16_t g_pass, g_fail;
void tlog(const char *fmt, ...);
void flog(const char *name);
#define CHECK(cond, name) do { if (cond) { ++g_pass; tlog("PASS %s\n", name); } \
                               else { ++g_fail; tlog("FAIL %s\n", name); flog(name); } } while (0)
extern "C" void done() __attribute__((noinline));
