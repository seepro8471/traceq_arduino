#include "TraceQ_Arduino/rfid/RfidController.hpp"

#include <string.h>

namespace
{
constexpr uint8_t kDefaultAccessKey[6] = {0x90, 0x25, 0x84, 0x71, 0x84, 0x72};
}

RfidController::RfidController()
    : mMfrc522(PIN_RFID, PIN_RESET)
{
    for (uint8_t i = 0; i < 6; ++i)
    {
        mAccessKey.keyByte[i]  = kDefaultAccessKey[i];
        mDefaultKey.keyByte[i] = 0xFF;
    }
}

void RfidController::Initialize()
{
    pinMode(PIN_RFID, OUTPUT);
    digitalWrite(PIN_RFID, HIGH);
    pinMode(PIN_RESET, OUTPUT);

    if (digitalRead(PIN_RESET) == LOW)
    {
        digitalWrite(PIN_RESET, HIGH);
        delay(50); // 하드 리셋 안정화. 데이터시트 권장.
    }
    else
    {
        pcdSoftReset();
    }

    mMfrc522.PCD_WriteRegister(MFRC522::TxModeReg, 0x80);
    mMfrc522.PCD_WriteRegister(MFRC522::TPrescalerReg, 0xA9);
    mMfrc522.PCD_WriteRegister(MFRC522::TReloadRegH, 0x03);
    mMfrc522.PCD_WriteRegister(MFRC522::TReloadRegL, 0xE8);
    mMfrc522.PCD_WriteRegister(MFRC522::TxASKReg, 0x40);
    mMfrc522.PCD_WriteRegister(MFRC522::ModeReg, 0x3D);
    mMfrc522.PCD_AntennaOn();

    invalidateAuthCache();
    mInitialized = true;
}

void RfidController::Reinitialize()
{
    EndSession();
    mInitialized = false;
    Initialize();
}

bool RfidController::IsAlive()
{
    const auto v = mMfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    return (v != 0x00) && (v != 0xFF);
}

void RfidController::pcdSoftReset()
{
    mMfrc522.PCD_WriteRegister(MFRC522::CommandReg, MFRC522::PCD_SoftReset);
    // 데이터시트 기준 soft reset 후 PowerDown 비트가 클리어될 때까지 대기.
    // 1.0은 delay(50)*3로 최대 150ms 고정. 2.0은 마이크로초 폴링으로 보통 수 ms 안에 종료.
    const unsigned long deadline = millis() + TRACEQ_RFID_SOFTRESET_TIMEOUT_MS;
    while (millis() < deadline)
    {
        if ((mMfrc522.PCD_ReadRegister(MFRC522::CommandReg) & (1 << 4)) == 0)
        {
            return;
        }
        delayMicroseconds(200);
    }
    // 타임아웃 시에도 진행: 다음 레지스터 쓰기에서 실패하면 상위에서 IsAlive로 감지.
}

RfidController::TagStatus RfidController::Poll()
{
    if (!mInitialized)
    {
        return TagStatus::NotYetConnected;
    }

    mTagPresentPrev = mTagPresent;

    byte bufferATQA[2];
    byte bufferSize{sizeof(bufferATQA)};

    // 1.0과 동일하게 RequestA 전 baudrate/ModWidth 복원 (이전 인증 후 변경된 상태 복구).
    mMfrc522.PCD_WriteRegister(MFRC522::TxModeReg, 0x00);
    mMfrc522.PCD_WriteRegister(MFRC522::RxModeReg, 0x00);
    mMfrc522.PCD_WriteRegister(MFRC522::ModWidthReg, 0x26);

    mLastStatus = mMfrc522.PICC_RequestA(bufferATQA, &bufferSize);
    const bool present =
        (mLastStatus == MFRC522::STATUS_OK || mLastStatus == MFRC522::STATUS_COLLISION) &&
        mMfrc522.PICC_ReadCardSerial();

    mTagPresent = present;

    if (!present)
    {
        // 태그가 사라진 시점이면 캐시도 무효화.
        if (mTagPresentPrev)
        {
            invalidateAuthCache();
            return TagStatus::Disconnected;
        }
        return TagStatus::NotYetConnected;
    }

    const auto pt = MFRC522::PICC_GetType(mMfrc522.uid.sak);
    if (pt != MFRC522::PICC_TYPE_MIFARE_1K && pt != MFRC522::PICC_TYPE_MIFARE_4K)
    {
        return TagStatus::Invalid;
    }

    // UID가 이전과 다르면 새로 들어온 태그 → 캐시 무효.
    bool sameUid = (mAuthUidSize == mMfrc522.uid.size) &&
                   (memcmp(mAuthUid, mMfrc522.uid.uidByte, mAuthUidSize) == 0);
    if (!sameUid)
    {
        invalidateAuthCache();
    }

    return mTagPresentPrev ? TagStatus::KeepAlive : TagStatus::Connected;
}

void RfidController::invalidateAuthCache()
{
    if (mAuthValid)
    {
        // 1.0의 가장 큰 안전성 이슈: 인증 후 StopCrypto1을 호출하지 않으면
        // 다음 통신이 암호화 상태로 남아 알 수 없는 실패를 만든다. 항상 정리.
        mMfrc522.PCD_StopCrypto1();
    }
    mAuthValid = false;
    mAuthSector = 0xFF;
}

void RfidController::EndSession()
{
    if (mAuthValid)
    {
        mMfrc522.PICC_HaltA();
        mMfrc522.PCD_StopCrypto1();
    }
    else
    {
        // Halt만 단독으로 호출해도 안전.
        mMfrc522.PICC_HaltA();
    }
    mAuthValid = false;
    mAuthSector = 0xFF;
    mAuthUidSize = 0;
    // 주의: mTagPresent는 지우지 않는다. 지우면 태그를 계속 대고 있을 때
    // 다음 Poll이 또 Connected를 반환해 같은 태그가 반복 처리된다(실기 확인).
    // 1.0은 present/prev 플래그를 유지해 "올려둔 태그는 1회만 처리"였다 —
    // 플래그를 유지하면 유지 중엔 KeepAlive, 뗐다 다시 대면 Connected가 된다.
}

void RfidController::SetAccessKey(const uint8_t key[6])
{
    for (uint8_t i = 0; i < 6; ++i)
    {
        mAccessKey.keyByte[i] = key[i];
    }
    invalidateAuthCache(); // 키가 바뀌면 캐시도 무효.
}

bool RfidController::ensureAuthenticated(uint8_t block)
{
    const uint8_t sector = SectorOfBlock(block);

    // 캐시 hit: UID 동일 + 섹터 동일 → 재인증 skip (성능 핵심).
    if (mAuthValid && mAuthSector == sector &&
        mAuthUidSize == mMfrc522.uid.size &&
        memcmp(mAuthUid, mMfrc522.uid.uidByte, mAuthUidSize) == 0)
    {
        return true;
    }

    // 다른 섹터로 이동: StopCrypto1 없이 그대로 재인증(nested authentication).
    // 카드는 인증 후 암호화 상태를 유지하므로, 리더만 StopCrypto1로 평문이 되면
    // 카드가 암호화된 인증 명령을 기대해 두 번째 섹터부터 전부 AuthFailed 가 된다
    // (실기 확인: 섹터0 성공 → 섹터1 인증 실패). 1.0도, MFRC522 라이브러리의
    // 덤프 예제도 중간 StopCrypto1 없이 섹터를 연속 인증한다.
    if (mAuthValid)
    {
        mAuthValid = false;
    }

    // 트레일러 블록 주소로 인증해야 섹터 전체에 대한 권한 부여.
    const uint8_t trailer = TrailerOfSector(sector);
    mLastStatus = mMfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailer, &mAccessKey, &mMfrc522.uid);

    if (mLastStatus != MFRC522::STATUS_OK)
    {
        // 인증 실패 시 암호화 상태가 어중간하게 남을 수 있으므로 명시적 정리.
        mMfrc522.PCD_StopCrypto1();
        return false;
    }

    mAuthValid = true;
    mAuthSector = sector;
    mAuthUidSize = mMfrc522.uid.size;
    memcpy(mAuthUid, mMfrc522.uid.uidByte, mAuthUidSize);
    return true;
}

RfidResult RfidController::Read(uint8_t block, void *out, uint8_t outSize)
{
    if (!mInitialized) return RfidResult::NotInitialized;
    if (out == nullptr || outSize == 0 || outSize > MIFARE_BLOCK_SIZE)
        return RfidResult::InvalidArgument;

    if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;

    uint8_t buf[MIFARE_READ_BUFFER_SIZE]{};
    byte size = sizeof(buf);
    mLastStatus = mMfrc522.MIFARE_Read(block, buf, &size);
    if (mLastStatus != MFRC522::STATUS_OK)
    {
        invalidateAuthCache(); // 실패 시 안전하게 캐시 폐기.
        return RfidResult::ReadFailed;
    }

    memcpy(out, buf, outSize);
    return RfidResult::Ok;
}

RfidResult RfidController::writeOnce(uint8_t block, const uint8_t buffer[16])
{
    mLastStatus = mMfrc522.MIFARE_Write(block, const_cast<uint8_t *>(buffer), MIFARE_BLOCK_SIZE);
    return (mLastStatus == MFRC522::STATUS_OK) ? RfidResult::Ok : RfidResult::WriteFailed;
}

bool RfidController::verifyBlock(uint8_t block, const uint8_t expected[16])
{
    uint8_t buf[MIFARE_READ_BUFFER_SIZE]{};
    byte size = sizeof(buf);
    mLastStatus = mMfrc522.MIFARE_Read(block, buf, &size);
    if (mLastStatus != MFRC522::STATUS_OK) return false;
    return memcmp(buf, expected, MIFARE_BLOCK_SIZE) == 0;
}

RfidResult RfidController::Write(uint8_t block, const void *value, uint8_t size)
{
    if (!mInitialized) return RfidResult::NotInitialized;
    if (value == nullptr || size == 0 || size > MIFARE_BLOCK_SIZE)
        return RfidResult::InvalidArgument;
    if (IsManufacturerBlock(block) || IsSectorTrailer(block))
        return RfidResult::ProtectedBlock;

    uint8_t buffer[MIFARE_BLOCK_SIZE]{};
    memcpy(buffer, value, size); // size<16이면 zero-pad.

    if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;

    RfidResult lastError = RfidResult::WriteFailed;
    for (uint8_t attempt = 0; attempt < TRACEQ_RFID_MAX_WRITE_RETRIES; ++attempt)
    {
        RfidResult r = writeOnce(block, buffer);
        if (r == RfidResult::Ok)
        {
#if TRACEQ_RFID_VERIFY_WRITES
            if (verifyBlock(block, buffer)) return RfidResult::Ok;
            // verify 실패 → 재시도. 캐시만 무효화하고 nested 재인증
            // (StopCrypto1을 하면 카드의 암호화 상태와 어긋나 재인증이 실패한다).
            lastError = RfidResult::VerifyMismatch;
            mAuthValid = false;
            if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
            continue;
#else
            return RfidResult::Ok;
#endif
        }
        // 쓰기 자체 실패 → nested 재인증 후 재시도.
        lastError = RfidResult::WriteFailed;
        mAuthValid = false;
        if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
    }
    return lastError;
}

RfidResult RfidController::WriteBlocks(uint8_t startBlock, uint8_t count,
                                       const void *values, uint8_t stride)
{
    if (!mInitialized) return RfidResult::NotInitialized;
    if (values == nullptr || count == 0 || stride == 0 || stride > MIFARE_BLOCK_SIZE)
        return RfidResult::InvalidArgument;

    const uint8_t *src = static_cast<const uint8_t *>(values);
    uint8_t buffer[MIFARE_BLOCK_SIZE];

    for (uint8_t i = 0; i < count; ++i)
    {
        const uint8_t block = startBlock + i;

        if (IsManufacturerBlock(block) || IsSectorTrailer(block))
        {
            // 트레일러는 자동 skip — 호출자가 startBlock~count 범위에 트레일러를
            // 포함시키더라도 안전하게 다음 블록으로 진행.
            src += stride;
            continue;
        }

        memset(buffer, 0, MIFARE_BLOCK_SIZE);
        memcpy(buffer, src, stride);
        src += stride;

        // ensureAuthenticated가 섹터 캐시로 인증 1회만 수행 — 1.0 대비 핵심 개선.
        if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;

        bool ok = false;
        RfidResult lastError = RfidResult::WriteFailed;
        for (uint8_t attempt = 0; attempt < TRACEQ_RFID_MAX_WRITE_RETRIES; ++attempt)
        {
            if (writeOnce(block, buffer) == RfidResult::Ok)
            {
#if TRACEQ_RFID_VERIFY_WRITES
                if (verifyBlock(block, buffer)) { ok = true; break; }
                lastError = RfidResult::VerifyMismatch;
                mAuthValid = false; // nested 재인증 (StopCrypto1 금지 — 카드 암호화 상태 유지)
                if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
#else
                ok = true; break;
#endif
            }
            else
            {
                lastError = RfidResult::WriteFailed;
                mAuthValid = false; // nested 재인증 (StopCrypto1 금지 — 카드 암호화 상태 유지)
                if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
            }
        }
        if (!ok) return lastError;
    }
    return RfidResult::Ok;
}

RfidResult RfidController::Clear(uint8_t block)
{
    if (IsManufacturerBlock(block) || IsSectorTrailer(block))
        return RfidResult::ProtectedBlock;
    uint8_t zero[MIFARE_BLOCK_SIZE]{};
    return Write(block, zero, MIFARE_BLOCK_SIZE);
}

RfidResult RfidController::ClearSector(uint8_t sector)
{
    if (!mInitialized) return RfidResult::NotInitialized;
    const uint8_t trailer = TrailerOfSector(sector);
    const uint8_t firstData = (sector == 0)
        ? 1                                      // 섹터0: 제조사 블록 0 회피.
        : static_cast<uint8_t>(sector * MIFARE_BLOCKS_PER_SECTOR);
    uint8_t zero[MIFARE_BLOCK_SIZE]{};

    for (uint8_t blk = firstData; blk < trailer; ++blk)
    {
        RfidResult r = Write(blk, zero, MIFARE_BLOCK_SIZE);
        if (r != RfidResult::Ok) return r;
    }
    return RfidResult::Ok;
}

RfidResult RfidController::ClearTag()
{
    for (uint8_t s = 0; s < MIFARE_1K_SECTOR_COUNT; ++s)
    {
        RfidResult r = ClearSector(s);
        if (r != RfidResult::Ok) return r;
    }
    return RfidResult::Ok;
}

RfidResult RfidController::WriteTrailer(uint8_t trailerBlock, const uint8_t trailerData[16])
{
    if (!mInitialized) return RfidResult::NotInitialized;
    if (trailerData == nullptr) return RfidResult::InvalidArgument;
    if (!IsSectorTrailer(trailerBlock)) return RfidResult::InvalidArgument;

    if (!ensureAuthenticated(trailerBlock)) return RfidResult::AuthFailed;

    mLastStatus = mMfrc522.MIFARE_Write(
        trailerBlock, const_cast<uint8_t *>(trailerData), MIFARE_BLOCK_SIZE);

    if (mLastStatus != MFRC522::STATUS_OK) return RfidResult::WriteFailed;
    // 트레일러 변경 후에는 키가 바뀌었을 수 있으므로 캐시 무효.
    invalidateAuthCache();
    return RfidResult::Ok;
}

RfidResult RfidController::InstallTraceQKeys()
{
    if (!mInitialized) return RfidResult::NotInitialized;

    // 1.0 `RfidScanner::SetAccessMethod`와 동일한 트레일러 레이아웃:
    // [KEY_A=FF×6 유지][AccessBits g0~g3=3][GPB][KEY_B=AccessKey]
    uint8_t trailerBuffer[MIFARE_BLOCK_SIZE] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, // KEY_A는 공장 기본 유지
        0, 0, 0,
        0,
        0, 0, 0, 0, 0, 0,
    };
    memcpy(&trailerBuffer[10], mAccessKey.keyByte, 6);
    mMfrc522.MIFARE_SetAccessBits(&trailerBuffer[6], 3, 3, 3, 3);

    // 진행 전 기존 crypto 상태 정리. (1.0은 연속 PCD_Authenticate로 진행 — 동일 순서 유지)
    invalidateAuthCache();

    for (uint8_t sector = 0; sector < MIFARE_1K_SECTOR_COUNT; ++sector)
    {
        const uint8_t trailer = TrailerOfSector(sector);
        mLastStatus = mMfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailer, &mDefaultKey, &mMfrc522.uid);
        if (mLastStatus != MFRC522::STATUS_OK)
        {
            mMfrc522.PCD_StopCrypto1();
            return RfidResult::AuthFailed;
        }
        mLastStatus = mMfrc522.MIFARE_Write(trailer, trailerBuffer, MIFARE_BLOCK_SIZE);
        if (mLastStatus != MFRC522::STATUS_OK)
        {
            mMfrc522.PCD_StopCrypto1();
            return RfidResult::WriteFailed;
        }
    }
    mMfrc522.PCD_StopCrypto1();
    return RfidResult::Ok;
}

RfidResult RfidController::RestoreFactoryKeys()
{
    if (!mInitialized) return RfidResult::NotInitialized;

    // 1.0 `RfidScanner::InitAccessMethod`와 동일한 공장 기본 트레일러:
    // [KEY_A=FF×6][AccessBits (0,0,0,1)][GPB][KEY_B=FF×6]
    uint8_t trailerBuffer[MIFARE_BLOCK_SIZE] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0, 0, 0,
        0,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    mMfrc522.MIFARE_SetAccessBits(&trailerBuffer[6], 0, 0, 0, 1);

    invalidateAuthCache();

    for (uint8_t sector = 0; sector < MIFARE_1K_SECTOR_COUNT; ++sector)
    {
        const uint8_t trailer = TrailerOfSector(sector);
        mLastStatus = mMfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailer, &mAccessKey, &mMfrc522.uid);
        if (mLastStatus != MFRC522::STATUS_OK)
        {
            mMfrc522.PCD_StopCrypto1();
            return RfidResult::AuthFailed;
        }
        mLastStatus = mMfrc522.MIFARE_Write(trailer, trailerBuffer, MIFARE_BLOCK_SIZE);
        if (mLastStatus != MFRC522::STATUS_OK)
        {
            mMfrc522.PCD_StopCrypto1();
            return RfidResult::WriteFailed;
        }
    }
    mMfrc522.PCD_StopCrypto1();
    return RfidResult::Ok;
}
