// RegisterProbe — 속도 개선 전 실측용 진단 스케치 (일회성)
//
// 목적 3가지 (전부 "추정 대신 실측"):
//   1. SPI 실동작 속도: SPCR/SPSR 분주 비트를 읽어 실제 clock 을 산출.
//   2. TModeReg/TxModeReg 실제 값 — 하드웨어 25ms 타임아웃(TAuto)이 켜져 있는지.
//   3. 태그 처리 구간별 소요 시간(µs): Poll / 인증+블록읽기 / 블록쓰기 /
//      EndSession(HaltA) — 어디에 시간이 쓰이는지.
//
// 사용:
//   platformio.ini 의 [env:probe] 로 빌드·업로드 후 시리얼 모니터 115200.
//   태그를 대면 측정값이 출력된다. 측정이 끝나면 원래 펌웨어를 다시 올릴 것.

#include <Arduino.h>
#include <SPI.h>

#include "TraceQ_Arduino.hpp"

RfidController rfid{};

static uint32_t spi_clock_hz()
{
    // SPCR[1:0] = SPR1:SPR0, SPSR[0] = SPI2X
    const uint8_t spr = SPCR & 0x03;
    const uint8_t spi2x = SPSR & 0x01;
    uint16_t div = 4;
    switch (spr)
    {
    case 0: div = 4;   break;
    case 1: div = 16;  break;
    case 2: div = 64;  break;
    default: div = 128; break;
    }
    if (spi2x) div /= 2;
    return F_CPU / div;
}

static void dump_registers(const char *label)
{
    // 레지스터를 읽는 것 자체가 SPI 트랜잭션이라 SPCR 이 그때 값으로 남는다.
    const uint8_t ver   = rfid.ReadReg(MFRC522::VersionReg);
    const uint8_t tmode = rfid.ReadReg(MFRC522::TModeReg);
    const uint8_t txmod = rfid.ReadReg(MFRC522::TxModeReg);
    const uint8_t tpre  = rfid.ReadReg(MFRC522::TPrescalerReg);
    const uint8_t trh   = rfid.ReadReg(MFRC522::TReloadRegH);
    const uint8_t trl   = rfid.ReadReg(MFRC522::TReloadRegL);

    Serial.print(F("["));
    Serial.print(label);
    Serial.println(F("]"));
    Serial.print(F("  SPI clock  = ")); Serial.print(spi_clock_hz()); Serial.println(F(" Hz"));
    Serial.print(F("  SPCR=0x")); Serial.print(SPCR, HEX);
    Serial.print(F(" SPSR=0x"));  Serial.println(SPSR, HEX);
    Serial.print(F("  VersionReg   = 0x")); Serial.println(ver, HEX);
    Serial.print(F("  TModeReg     = 0x")); Serial.print(tmode, HEX);
    Serial.println((tmode & 0x80) ? F("  (TAuto ON)") : F("  (TAuto OFF <- 하드웨어 타임아웃 꺼짐)"));
    Serial.print(F("  TxModeReg    = 0x")); Serial.println(txmod, HEX);
    Serial.print(F("  TPrescaler   = 0x")); Serial.println(tpre, HEX);
    Serial.print(F("  TReload      = ")); Serial.println((uint16_t)((trh << 8) | trl));
}

void setup()
{
    Serial.begin(115200);
    SPI.begin();
    delay(300);
    Serial.println();
    Serial.println(F("=== TraceQ RegisterProbe ==="));
    Serial.print(F("firmware version = ")); Serial.println(TRACEQ_VERSION_STRING);

    rfid.Initialize();
    dump_registers("Initialize 직후");

    // 빈 폴링(태그 없음) 1회 소요 — REQA 무응답 대기 비용.
    unsigned long t0 = micros();
    rfid.Poll();
    unsigned long emptyPoll = micros() - t0;
    Serial.print(F("빈 Poll(태그 없음) = ")); Serial.print(emptyPoll); Serial.println(F(" us"));
    dump_registers("Poll 직후");

    Serial.println(F("--- 태그를 대주세요 ---"));
}

void loop()
{
    unsigned long t0 = micros();
    const auto st = rfid.Poll();
    const unsigned long pollUs = micros() - t0;
    if (st != RfidController::TagStatus::Connected)
    {
        delay(30);
        return;
    }

    Serial.println();
    Serial.print(F("Poll(감지)        = ")); Serial.print(pollUs); Serial.println(F(" us"));

    // 인증 + 블록 읽기 (섹터0)
    Company company{};
    t0 = micros();
    const RfidResult r1 = rfid.Read(SECTOR0_COMPANY, &company, sizeof(Company));
    const unsigned long readFirstUs = micros() - t0;
    Serial.print(F("첫 Read(인증 포함) = ")); Serial.print(readFirstUs);
    Serial.print(F(" us  result=")); Serial.println(RfidResultName(r1));

    // 같은 섹터 두 번째 읽기 (인증 캐시 hit)
    Tag tag{};
    t0 = micros();
    const RfidResult r2 = rfid.Read(SECTOR0_TAG, &tag, sizeof(Tag));
    const unsigned long readCachedUs = micros() - t0;
    Serial.print(F("같은 섹터 Read     = ")); Serial.print(readCachedUs);
    Serial.print(F(" us  result=")); Serial.println(RfidResultName(r2));

    // 다른 섹터 읽기 (nested 재인증)
    TagSerial serial16{};
    t0 = micros();
    const RfidResult r3 = rfid.Read(SECTOR1_TAG_SERIAL, &serial16, sizeof(TagSerial));
    const unsigned long readNestedUs = micros() - t0;
    Serial.print(F("다른 섹터 Read     = ")); Serial.print(readNestedUs);
    Serial.print(F(" us  result=")); Serial.println(RfidResultName(r3));

    // 쓰기 (읽은 값을 그대로 되쓰기 — 데이터 불변, verify 포함)
    t0 = micros();
    const RfidResult r4 = rfid.Write(SECTOR1_TAG_SERIAL, &serial16, sizeof(TagSerial));
    const unsigned long writeUs = micros() - t0;
    Serial.print(F("Write(verify 포함) = ")); Serial.print(writeUs);
    Serial.print(F(" us  result=")); Serial.println(RfidResultName(r4));

    // 세션 종료 (HaltA + StopCrypto1)
    t0 = micros();
    rfid.EndSession();
    const unsigned long endUs = micros() - t0;
    Serial.print(F("EndSession(HaltA)  = ")); Serial.print(endUs); Serial.println(F(" us"));

    Serial.println(F("--- 태그를 떼었다 다시 대면 반복 ---"));
    delay(500);
}
