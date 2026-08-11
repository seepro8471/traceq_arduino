// IntegrityTest — SPI 속도별 태그 읽기/쓰기 데이터 무결성 실측 (일회성 진단)
//
// ★이 스케치에는 LCD 표시도 버튼 처리도 없다 (시리얼 전용).
//   업로드하면 화면이 멎고 버튼이 안 먹는 것이 정상이며, 측정이 끝나면
//   반드시 기본 env(정상 펌웨어)를 다시 업로드할 것.
//
// 검사 3종:
//   1) 태그 실데이터 덤프 — 지금 태그의 내용이 온전한지 눈으로 확인.
//   2) 읽기 무결성 — 같은 블록을 N회 읽어 첫 결과와 전수 비교(불일치 = SPI 오염).
//   3) 쓰기 무결성 — **스크래치 블록(52)** 에만 난수 16B 를 쓰고 되읽어 비교.
//      블록 52 는 구버전 호환용으로 서버 전송 때 어차피 0 으로 지워 보내는
//      자리라(SerialProcessor::legacy_loop_process) 실데이터가 아니다.
//      검사 끝에 0 으로 되돌린다.
//
// 사용: py -m platformio run -e integrity -t upload --upload-port COMx
//       py tools\read_probe.py COMx 120     (태그를 계속 대고 있을 것)

#include <Arduino.h>
#include <SPI.h>

#include "TraceQ_Arduino.hpp"

RfidController rfid{};

// 스크래치 = 블록 18 (섹터4). "이전 소독기 정보"용이었으나 현재는 쓰지 않고
// 서버 전송 때 0 으로 지워 보내는 자리다(SerialProcessor::legacy_loop_process).
// 같은 섹터(4)에 세척종료 담당자 블록 16·17 이 있어 TraceQ 키가 확실히 걸려 있다.
// (블록 52/섹터13 은 태그에 따라 TraceQ 키가 안 걸려 있어 인증이 안 된다 — 실측)
constexpr uint8_t SCRATCH_BLOCK{18};
constexpr uint16_t READ_ITERATIONS{200};
constexpr uint16_t WRITE_ITERATIONS{100};

static uint32_t spi_clock_hz()
{
    const uint8_t spr = SPCR & 0x03;
    const uint8_t spi2x = SPSR & 0x01;
    uint16_t div = (spr == 0) ? 4 : (spr == 1) ? 16 : (spr == 2) ? 64 : 128;
    if (spi2x) div /= 2;
    return F_CPU / div;
}

static void print_hex16(const uint8_t *p)
{
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (p[i] < 0x10) Serial.print('0');
        Serial.print(p[i], HEX);
        Serial.print(' ');
    }
}

static void dump_block(const char *label, uint8_t block)
{
    uint8_t buf[16]{};
    const RfidResult r = rfid.Read(block, buf, 16);
    Serial.print(F("  "));
    Serial.print(label);
    Serial.print(F(" (blk "));
    Serial.print(block);
    Serial.print(F(") "));
    if (r != RfidResult::Ok)
    {
        Serial.print(F("읽기 실패: "));
        Serial.println(RfidResultName(r));
        return;
    }
    print_hex16(buf);
    Serial.println();
}

void setup()
{
    Serial.begin(115200);
    SPI.begin();
    delay(300);
    Serial.println();
    Serial.println(F("=== TraceQ IntegrityTest ==="));
    Serial.print(F("firmware = ")); Serial.println(TRACEQ_VERSION_STRING);
    rfid.Initialize();
    Serial.print(F("SPI clock = ")); Serial.print(spi_clock_hz()); Serial.println(F(" Hz"));
    Serial.print(F("RC522 VersionReg = 0x"));
    Serial.print(rfid.ReadReg(MFRC522::VersionReg), HEX);
    Serial.println(F("  (0x91/0x92=정품 NXP, 그 외=호환칩)"));
    Serial.println(F("--- 태그를 대고 그대로 두세요 (약 1분) ---"));
}

void loop()
{
    if (rfid.Poll() != RfidController::TagStatus::Connected)
    {
        delay(30);
        return;
    }

    Serial.println();
    Serial.println(F("### 1) 태그 실데이터 덤프 ###"));
    dump_block("Company     ", SECTOR0_COMPANY);
    dump_block("Tag(번호/ID)", SECTOR0_TAG);
    dump_block("Serial      ", SECTOR1_TAG_SERIAL);
    dump_block("Gateway     ", SECTOR1_GATEWAY);
    dump_block("Process     ", SECTOR1_PROCESS);
    dump_block("환자키      ", SECTOR2_PATIENT_KEY);
    dump_block("세척시작    ", SECTOR2_WASHING_START);
    dump_block("소독시작    ", SECTOR5_DISINFECTION_START);

    // ── 2) 읽기 무결성 ─────────────────────────────────────────────
    Serial.println();
    Serial.println(F("### 2) 읽기 무결성 ###"));
    uint8_t first[16]{};
    if (rfid.Read(SECTOR0_TAG, first, 16) != RfidResult::Ok)
    {
        Serial.println(F("  기준 읽기 실패 — 태그를 다시 대주세요"));
        rfid.EndSession();
        delay(1000);
        return;
    }
    uint16_t readFail = 0, readMismatch = 0;
    unsigned long t0 = micros();
    for (uint16_t i = 0; i < READ_ITERATIONS; ++i)
    {
        uint8_t buf[16]{};
        const RfidResult r = rfid.Read(SECTOR0_TAG, buf, 16);
        if (r != RfidResult::Ok) { ++readFail; continue; }
        if (memcmp(buf, first, 16) != 0) ++readMismatch;
    }
    const unsigned long readUs = micros() - t0;
    Serial.print(F("  ")); Serial.print(READ_ITERATIONS); Serial.print(F("회 읽기 — 실패 "));
    Serial.print(readFail); Serial.print(F("건, 내용 불일치 "));
    Serial.print(readMismatch); Serial.print(F("건, 평균 "));
    Serial.print(readUs / READ_ITERATIONS); Serial.println(F(" us"));

    // ── 3) 쓰기 무결성 (스크래치 블록) ──────────────────────────────
    Serial.println();
    Serial.print(F("### 3) 쓰기 무결성 (스크래치 블록 "));
    Serial.print(SCRATCH_BLOCK);
    Serial.println(F(") ###"));

    // 스크래치 원본 보존 — 검사 후 그대로 되돌린다.
    uint8_t scratchOrig[16]{};
    const RfidResult so = rfid.Read(SCRATCH_BLOCK, scratchOrig, 16);
    if (so != RfidResult::Ok)
    {
        Serial.print(F("  스크래치 블록 접근 불가: "));
        Serial.println(RfidResultName(so));
        rfid.EndSession();
        delay(2000);
        return;
    }

    uint16_t writeFail = 0, verifyMismatch = 0, authFail = 0;
    uint32_t seed = 0x1234ABCDUL;
    t0 = micros();
    for (uint16_t i = 0; i < WRITE_ITERATIONS; ++i)
    {
        uint8_t pattern[16];
        for (uint8_t b = 0; b < 16; ++b)
        {
            seed = seed * 1664525UL + 1013904223UL;   // LCG
            pattern[b] = static_cast<uint8_t>(seed >> 24);
        }
        const RfidResult w = rfid.Write(SCRATCH_BLOCK, pattern, 16);
        if (w == RfidResult::AuthFailed) { ++authFail; continue; }
        if (w != RfidResult::Ok) { ++writeFail; continue; }

        uint8_t back[16]{};
        if (rfid.Read(SCRATCH_BLOCK, back, 16) != RfidResult::Ok) { ++readFail; continue; }
        if (memcmp(back, pattern, 16) != 0) ++verifyMismatch;
    }
    const unsigned long writeUs = micros() - t0;
    Serial.print(F("  ")); Serial.print(WRITE_ITERATIONS);
    Serial.print(F("회 쓰기+되읽기 — 쓰기실패 "));
    Serial.print(writeFail); Serial.print(F("건, 인증실패 "));
    Serial.print(authFail); Serial.print(F("건, ★내용 불일치 "));
    Serial.print(verifyMismatch); Serial.print(F("건, 평균 "));
    Serial.print(writeUs / WRITE_ITERATIONS); Serial.println(F(" us"));

    // 스크래치 블록 원상복구 (검사 시작 시점의 원본 그대로)
    const RfidResult z = rfid.Write(SCRATCH_BLOCK, scratchOrig, 16);
    Serial.print(F("  스크래치 원본 복구: "));
    Serial.println(RfidResultName(z));

    Serial.println();
    Serial.println(F("### 판정 ###"));
    if (readFail == 0 && readMismatch == 0 && writeFail == 0 &&
        verifyMismatch == 0 && authFail == 0)
        Serial.println(F("  이 속도에서 오류 0건 — 안전"));
    else
        Serial.println(F("  ★오류 발생 — 이 속도는 채택하지 말 것"));

    rfid.EndSession();
    Serial.println(F("--- 태그를 떼었다 다시 대면 재측정 ---"));
    delay(2000);
}
