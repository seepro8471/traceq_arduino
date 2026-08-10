// RfidWriteBenchmark — 2.0의 일괄 쓰기 성능을 측정.
//
// 사용: 메인 src/main.cpp 대신 이 파일을 빌드하거나, platformio.ini에
//   src_filter = +<../examples/RfidWriteBenchmark/>
// 를 추가해 사용한다.
//
// 동작:
//   1) 새 태그가 감지되면 SECTOR1의 데이터 블록(4,5,6) 3개에 16바이트씩
//      WriteBlocks()로 일괄 기록 (인증 1회).
//   2) 같은 데이터를 단건 Write() × 3 으로 따로 기록 (인증 3회 — 비교군).
//   3) 두 방식의 micros 소요 시간을 시리얼로 출력.

#include <Arduino.h>
#include <SPI.h>

#include "TraceQ_Arduino.hpp"

RfidController rfid{};

static void hexdump(const uint8_t *p, uint8_t n)
{
    for (uint8_t i = 0; i < n; ++i)
    {
        if (p[i] < 0x10) Serial.print('0');
        Serial.print(p[i], HEX);
        Serial.print(' ');
    }
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    SPI.begin();
    rfid.Initialize();
    Serial.println(F("[RfidWriteBenchmark] ready — place a MIFARE Classic 1K tag."));
}

void loop()
{
    if (rfid.Poll() != RfidController::TagStatus::Connected)
    {
        delay(50);
        return;
    }

    uint8_t payload[3 * MIFARE_BLOCK_SIZE];
    for (uint8_t i = 0; i < sizeof(payload); ++i) payload[i] = i;

    // --- 2.0 일괄 쓰기 (인증 1회) ---
    unsigned long t0 = micros();
    RfidResult br = rfid.WriteBlocks(SECTOR1_TAG_SERIAL, 3, payload, MIFARE_BLOCK_SIZE);
    unsigned long batchUs = micros() - t0;

    Serial.print(F("WriteBlocks (sector-cached auth) : "));
    Serial.print(RfidResultName(br));
    Serial.print(F(" / "));
    Serial.print(batchUs);
    Serial.println(F(" us"));

    // 다음 비교를 위해 캐시 무효화 + 재태깅 시뮬레이션은 EndSession 으로.
    rfid.EndSession();

    // 주의: EndSession의 HaltA 이후 태그는 REQA에 응답하지 않으므로,
    // 2차 측정을 진행하려면 태그를 물리적으로 뗐다가 다시 대야 한다.
    while (rfid.Poll() != RfidController::TagStatus::Connected) { delay(10); }

    // --- 단건 Write × 3 (인증 캐시 비활성 시뮬레이션) ---
    unsigned long s0 = micros();
    for (uint8_t i = 0; i < 3; ++i)
    {
        RfidResult sr = rfid.Write(SECTOR1_TAG_SERIAL + i, &payload[i * MIFARE_BLOCK_SIZE], MIFARE_BLOCK_SIZE);
        if (sr != RfidResult::Ok)
        {
            Serial.print(F("Write block "));
            Serial.print(SECTOR1_TAG_SERIAL + i);
            Serial.print(F(" failed: "));
            Serial.println(RfidResultName(sr));
        }
        // 캐시 효과 무력화: 강제로 다른 섹터를 한 번 인증해 캐시 invalidation.
        // (payload를 읽기 버퍼로 쓰면 기록 데이터가 오염되므로 scratch 사용)
        uint8_t scratch{};
        rfid.Read(SECTOR0_COMPANY, &scratch, 1);
    }
    unsigned long singleUs = micros() - s0;
    Serial.print(F("Write x3 (forced re-auth each)   : "));
    Serial.print(singleUs);
    Serial.println(F(" us"));

    // 검증: 다시 읽어와 일치 확인.
    uint8_t verify[MIFARE_BLOCK_SIZE]{};
    for (uint8_t i = 0; i < 3; ++i)
    {
        if (rfid.Read(SECTOR1_TAG_SERIAL + i, verify, MIFARE_BLOCK_SIZE) == RfidResult::Ok)
        {
            Serial.print(F("blk "));
            Serial.print(SECTOR1_TAG_SERIAL + i);
            Serial.print(F(": "));
            hexdump(verify, MIFARE_BLOCK_SIZE);
        }
    }

    rfid.EndSession();
    Serial.println(F("--- remove tag to repeat ---"));
    delay(1500);
}
