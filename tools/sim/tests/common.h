#pragma once
// 시험 공용 — main.cpp 의 전역과 setup/loop/serialEvent 를 그대로 부른다.
#include "harness.h"
#include <EEPROM.h>
#include <new>
#include "TraceQ_Arduino.hpp"
#include <string.h>

void setup();
void loop();
void serialEvent();
extern char deviceType;
extern AlarmOption alarmOption;
extern DeviceOption deviceOption;
extern DisinfectionOption disinfectionOption;
extern ManagerOption managerOption;
extern RecordOption recordOption;
extern DefaultRtc rtc;
extern RfidController rfid;
extern DisplayClass lcd;
extern UserInterfaceClass ui;
extern Tag cachedTag;
extern TagSerial cachedTagSerial;
extern Process cachedProcess;
extern DisinfectionProcessor disinfectionProcessor;
extern GatewayProcessor gatewayProcessor;
extern SerialProcessor serialProcessor;
extern WashingProcessor washingProcessor;

// soft reset 이 나면 1, UI 가 버튼을 무한정 기다리면 2 를 돌려준다.
#define GUARDED(stmt) ([&]() -> int { g_resetArmed = true; int _r = setjmp(g_resetJmp); \
    if (_r == 0) { stmt; } g_resetArmed = false; return _r; }())

// "동기된 시계" 표본은 출시일(version.hpp)에서 유도한다 — 날짜를 박아 두면 출시일을 올릴 때마다
// IsUnsynced() 판정이 뒤집혀 시험이 깨진다(09-26 재발).
static inline DateTime rel_date(uint8_t h, uint8_t m, uint8_t s)
{
    return DateTime(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY, h, m, s);
}

// PC 가 보내는 시계 맞춤 JSON 도 출시일 기준으로 만든다(출시일보다 앞이면 '미동기' 로 판정된다).
static inline const char *rel_json_set_time(char *buf, size_t n, uint8_t h, uint8_t m, uint8_t s)
{
    snprintf(buf, n, "{\"cmd\":\"cfg_set_date_time\",\"device_date_time\":\"%04u-%02u-%02u %02u:%02u:%02u\"}",
             (unsigned)TRACEQ_RELEASE_YEAR, (unsigned)TRACEQ_RELEASE_MONTH, (unsigned)TRACEQ_RELEASE_DAY, h, m, s);
    return buf;
}
#define REL_YMD TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY

static inline void run_loops(uint8_t n)
{
    for (uint8_t i = 0; i < n; ++i) { GUARDED(loop()); sim_advance_ms(30); }
}

// 이미 초기화된 기기(기본값 + 타입 + 현재 도장)로 한 번 부팅 — setup() 은 실기처럼 1회만.
static inline uint32_t fw_stamp()   // main.cpp firmware_stamp() 와 같은 FNV-1a
{
    const char *s = TRACEQ_VERSION_STRING;
    uint32_t h = 2166136261UL;
    while (*s) { h ^= (uint8_t)*s++; h *= 16777619UL; }
    return h;
}
// 공장 출하 상태의 EEPROM(전부 0xFF) — 타입 원값이 W/D/S/G 가 아니므로 "쓰던 기기" 가 아니다.
static inline void eeprom_factory()
{
    for (int i = 0; i < EEPROM.length(); ++i) EEPROM.update(i, 0xFF);
}

static inline void boot(char type)
{
    g_rtcLostPower = false;
    alarmOption.Upload(); deviceOption.Upload(); disinfectionOption.Upload();
    managerOption.Upload(); recordOption.Upload();
    deviceOption.SetType(type);
    EEPROM.put((int)4088, fw_stamp());
    GUARDED(setup());
    run_loops(2);
}

// 진짜 리셋 — RAM 전역을 전부 다시 만든다(EEPROM·RTC 칩·카드는 그대로). setup() 만 다시 부르면
// RAM 표지가 살아남아 "재시작 뒤" 결함을 못 본다(3차 감사에서 실제로 놓쳤던 것).
static inline void hard_reset(bool rtcDead = false, uint8_t loops = 2)
{
    g_rtcLostPower = rtcDead;
    deviceType = 0;
    new (&alarmOption) AlarmOption{};
    new (&deviceOption) DeviceOption{};
    new (&disinfectionOption) DisinfectionOption{};
    new (&managerOption) ManagerOption{};
    new (&recordOption) RecordOption{};
    new (&rtc) DefaultRtc{};
    new (&rfid) RfidController{};
    new (&lcd) DisplayClass{0x27, 20, 4};
    new (&ui) UserInterfaceClass{lcd};
    new (&cachedTag) Tag{};
    new (&cachedTagSerial) TagSerial{};
    new (&cachedProcess) Process{};
    new (&disinfectionProcessor) DisinfectionProcessor{cachedTag, cachedTagSerial, cachedProcess, rfid};
    new (&gatewayProcessor) GatewayProcessor{cachedTag, cachedTagSerial, cachedProcess, rfid};
    new (&serialProcessor) SerialProcessor{cachedTag, cachedTagSerial, cachedProcess, rfid};
    new (&washingProcessor) WashingProcessor{cachedTag, cachedTagSerial, cachedProcess, rfid};
    GUARDED(setup());
    run_loops(loops);   // loops=0 이면 setup 직후 상태를 그대로 볼 수 있다
}

// 대기: 카드를 올려 두고 n 루프 → 떼고 n 루프
static inline void touch(SimCard &c, uint8_t loopsOn = 4, uint8_t loopsOff = 4)
{
    card_place(&c);
    run_loops(loopsOn);
    card_remove();
    run_loops(loopsOff);
}

// ── 태그 데이터 ──
static inline void put_block(SimCard &c, uint8_t b, const void *p, uint8_t n)
{
    memset(c.data[b], 0, 16);
    memcpy(c.data[b], p, n);
}
static inline void make_tag(SimCard &c, uint8_t uidLast, int tagType, int number,
                            const char *id, const char *serial)
{
    card_init_traceq(c, uidLast);
    Company co{};
    co.CompanyCode = TRACEQ_COMPANY_CODE;
    co.TagType = tagType;
    put_block(c, SECTOR0_COMPANY, &co, sizeof(co));
    Tag t{};
    t.Number = number;
    strncpy((char *)t.ID, id, sizeof(t.ID));
    put_block(c, SECTOR0_TAG, &t, sizeof(t));
    TagSerial s{};
    strncpy((char *)s.Serial, serial, sizeof(s.Serial) - 1);
    put_block(c, SECTOR1_TAG_SERIAL, &s, sizeof(s));
}
static inline Process get_process(const SimCard &c)
{
    Process p{};
    memcpy(&p, c.data[SECTOR1_PROCESS], 9);
    return p;
}
static inline void set_process(SimCard &c, const Process &p) { put_block(c, SECTOR1_PROCESS, &p, 9); }
static inline LocalDateTime get_ldt(const SimCard &c, uint8_t block)   // 레코드 = {int 번호, LocalDateTime}
{
    LocalDateTime t{};
    memcpy(&t, c.data[block] + 2, sizeof(t));
    return t;
}
static inline void set_record(SimCard &c, uint8_t block, int dev, const DateTime &dt)
{
    uint8_t buf[16]{};
    memcpy(buf, &dev, 2);
    LocalDateTime t = DefaultRtc::ToLocalDateTime(dt);
    memcpy(buf + 2, &t, sizeof(t));
    put_block(c, block, buf, 16);
}
static inline bool ldt_eq(const LocalDateTime &t, uint16_t y, uint8_t mo, uint8_t d,
                          uint8_t h, uint8_t mi, uint8_t s)
{
    return t.Date.Year == y && t.Date.Month == mo && t.Date.Day == d &&
           t.Time.Hour == h && t.Time.Minute == mi && t.Time.Second == s;
}
static inline void tlog_ldt(const char *label, const LocalDateTime &t)
{
    tlog("  %s = %04u-%02u-%02u %02u:%02u:%02u\n", label, t.Date.Year, t.Date.Month, t.Date.Day,
         t.Time.Hour, t.Time.Minute, t.Time.Second);
}
