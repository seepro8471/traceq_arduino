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

    // ★TModeReg 의 TAuto(0x80) — 송신 후 타이머 자동 시작. 이게 없으면
    //  TPrescaler/TReload 로 맞춰 둔 25ms 하드웨어 타임아웃이 절대 발화하지
    //  않아, 무응답(REQA 공회전·HaltA)마다 라이브러리의 소프트 카운터가 끝까지
    //  돌아 **288ms**를 태운다(실측). 1.0 은 같은 자리에 TxModeReg 를 써서
    //  (오탈자) 타이머가 꺼진 채 운영돼 왔다. TxModeReg 줄은 Poll 이 매번
    //  0x00 으로 덮어써 무의미하므로 그대로 두고, 한 번에 한 가지만 바꾼다.
    mMfrc522.PCD_WriteRegister(MFRC522::TModeReg, 0x80);
    mMfrc522.PCD_WriteRegister(MFRC522::TxModeReg, 0x80);
    mMfrc522.PCD_WriteRegister(MFRC522::TPrescalerReg, 0xA9);
    mMfrc522.PCD_WriteRegister(MFRC522::TReloadRegH, 0x03);
    mMfrc522.PCD_WriteRegister(MFRC522::TReloadRegL, 0xE8);
    mMfrc522.PCD_WriteRegister(MFRC522::TxASKReg, 0x40);
    mMfrc522.PCD_WriteRegister(MFRC522::ModeReg, 0x3D);
    mMfrc522.PCD_AntennaOn();

    stopCrypto();
    dropAuthCache();
    mAuthUidSize = 0;
    mInitialized = true;
}

void RfidController::Reinitialize()
{
    EndSession();
    mInitialized = false;
    mTagPresent = false;
    mTagPresentPrev = false;
    mMissCount = 0;
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

RfidController::TagStatus RfidController::Poll(bool wakeHalted)
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

    // WUPA 는 정지(HALT)된 카드도 깨운다 — 발급 대기에서만 쓴다(운영 루프가 쓰면 올려 둔 태그가 매번 다시 처리된다).
    mLastStatus = wakeHalted ? mMfrc522.PICC_WakeupA(bufferATQA, &bufferSize)
                             : mMfrc522.PICC_RequestA(bufferATQA, &bufferSize);
    const bool present =
        (mLastStatus == MFRC522::STATUS_OK || mLastStatus == MFRC522::STATUS_COLLISION) &&
        mMfrc522.PICC_ReadCardSerial();

    mTagPresent = present;

    // 정지(HALT) 못 시킨 카드는 REQA 에 한 번 걸러 응답한다(ACTIVE→IDLE) — 3회 연속 무응답만 이탈로 본다.
    // (1회로 보면 인증 실패·HaltA 유실 뒤 올려 둔 태그가 3루프마다 다시 처리된다.)
    if (!present && mTagPresentPrev && ++mMissCount < 3)
    {
        mTagPresent = true;
        return TagStatus::KeepAlive;
    }
    mMissCount = 0;

    if (!present)
    {
        // 태그가 사라진 시점이면 세션 종료 — 리더 암호화 상태까지 정리해야
        // 다음 태그의 REQA 가 성립한다.
        if (mTagPresentPrev)
        {
            stopCrypto();
            dropAuthCache();
            mAuthUidSize = 0;
            return TagStatus::Disconnected;
        }
        return TagStatus::NotYetConnected;
    }

    const auto pt = MFRC522::PICC_GetType(mMfrc522.uid.sak);
    if (pt != MFRC522::PICC_TYPE_MIFARE_1K && pt != MFRC522::PICC_TYPE_MIFARE_4K)
    {
        return TagStatus::Invalid;
    }

    // UID가 이전과 다르면 새로 들어온 태그 → 이전 세션을 완전히 정리.
    bool sameUid = (mAuthUidSize != 0) &&
                   (mAuthUidSize == mMfrc522.uid.size) &&
                   (memcmp(mAuthUid, mMfrc522.uid.uidByte, mAuthUidSize) == 0);
    if (!sameUid)
    {
        stopCrypto();
        dropAuthCache();
    }

    return mTagPresentPrev ? TagStatus::KeepAlive : TagStatus::Connected;
}

void RfidController::dropAuthCache()
{
    // 세션 내부 복구 — StopCrypto1 금지 (카드는 암호화 상태를 유지하므로
    // 리더만 평문이 되면 이후 nested authentication 이 전부 실패한다).
    mAuthValid = false;
    mAuthSector = 0xFF;
}

void RfidController::stopCrypto()
{
    // 세션 경계 — 무조건 해제. 리더가 암호화 상태로 굳으면 이후 REQA(Poll)
    // 가 전부 실패해 "리더가 죽은 것처럼" 보인다. 불필요한 호출은 무해하다.
    mMfrc522.PCD_StopCrypto1();
    mCryptoOn = false;
}

void RfidController::EndSession()
{
    // 순서 고정: HaltA(카드 세션 종료) → StopCrypto1(리더 평문화).
    mMfrc522.PICC_HaltA();
    stopCrypto();
    dropAuthCache();
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
    // 키가 바뀌면 진행 중인 세션도 의미가 없다 — 캐시·암호 상태 모두 정리.
    stopCrypto();
    dropAuthCache();
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
    dropAuthCache();

    // 트레일러 블록 주소로 인증해야 섹터 전체에 대한 권한 부여.
    const uint8_t trailer = TrailerOfSector(sector);
    mLastStatus = mMfrc522.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailer, &mAccessKey, &mMfrc522.uid);

    if (mLastStatus != MFRC522::STATUS_OK)
    {
        // 인증 실패 = 세션이 깨진 것 — 리더를 평문으로 되돌려야 다음 Poll(REQA)이 산다.
        stopCrypto();
        return false;
    }

    mCryptoOn  = true;
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
        // 세션 내부 실패 — 캐시만 버리고 nested 재인증 후 1회 재시도.
        // (StopCrypto1 을 하면 카드와 어긋나 이후 블록이 전부 실패한다.
        //  서버 덤프처럼 수십 블록을 연속으로 읽는 경로에서 1회 실패가
        //  나머지 전부를 0 으로 오염시키던 문제 — 2.2.5 수정)
        dropAuthCache();
        if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
        size = sizeof(buf);
        mLastStatus = mMfrc522.MIFARE_Read(block, buf, &size);
        if (mLastStatus != MFRC522::STATUS_OK)
        {
            dropAuthCache();
            return RfidResult::ReadFailed;
        }
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
            dropAuthCache();
            if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
            continue;
#else
            return RfidResult::Ok;
#endif
        }
        // 쓰기 자체 실패 → nested 재인증 후 재시도.
        lastError = RfidResult::WriteFailed;
        dropAuthCache();
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
                dropAuthCache(); // nested 재인증 (StopCrypto1 금지 — 카드 암호화 상태 유지)
                if (!ensureAuthenticated(block)) return RfidResult::AuthFailed;
#else
                ok = true; break;
#endif
            }
            else
            {
                lastError = RfidResult::WriteFailed;
                dropAuthCache(); // nested 재인증 (StopCrypto1 금지 — 카드 암호화 상태 유지)
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
    // (세션은 유지 — 다음 접근은 새 키로 nested 재인증한다.)
    dropAuthCache();
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

    // 1.0(SetAccessMethod)과 동일하게 StopCrypto1 없이 섹터를 연속 인증한다.
    // ★중간이나 끝에서 StopCrypto1 을 하면 카드는 인증 상태로 남고 리더만
    //  평문이 되어 직후 ClearTag()/Company 기록이 전부 AuthFailed 가 된다.
    //  정리는 호출측의 EndSession(HaltA→StopCrypto1)이 담당한다.
    dropAuthCache();

    for (uint8_t sector = 0; sector < MIFARE_1K_SECTOR_COUNT; ++sector)
    {
        const uint8_t trailer = TrailerOfSector(sector);
        mLastStatus = mMfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailer, &mDefaultKey, &mMfrc522.uid);
        if (mLastStatus != MFRC522::STATUS_OK) return RfidResult::AuthFailed;
        mCryptoOn = true;

        mLastStatus = mMfrc522.MIFARE_Write(trailer, trailerBuffer, MIFARE_BLOCK_SIZE);
        if (mLastStatus != MFRC522::STATUS_OK) return RfidResult::WriteFailed;
    }
    // 키가 바뀌었으므로 캐시만 버린다 (세션·암호화 상태는 유지).
    dropAuthCache();
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

    // 1.0(InitAccessMethod)과 동일 — 중간 StopCrypto1 금지 (위 Install 주석 참조).
    dropAuthCache();

    for (uint8_t sector = 0; sector < MIFARE_1K_SECTOR_COUNT; ++sector)
    {
        const uint8_t trailer = TrailerOfSector(sector);
        mLastStatus = mMfrc522.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_B, trailer, &mAccessKey, &mMfrc522.uid);
        if (mLastStatus != MFRC522::STATUS_OK) return RfidResult::AuthFailed;
        mCryptoOn = true;

        mLastStatus = mMfrc522.MIFARE_Write(trailer, trailerBuffer, MIFARE_BLOCK_SIZE);
        if (mLastStatus != MFRC522::STATUS_OK) return RfidResult::WriteFailed;
    }
    dropAuthCache();
    return RfidResult::Ok;
}
