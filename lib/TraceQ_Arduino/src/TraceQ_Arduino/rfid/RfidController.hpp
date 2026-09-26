#pragma once

#include <Arduino.h>
#include <MFRC522.h>

#include "TraceQ_Arduino/data/PinMap.hpp"
#include "TraceQ_Arduino/data/BlockMap.hpp"
#include "TraceQ_Arduino/rfid/RfidConfig.hpp"

/**
 * \class RfidController
 * \brief TraceQ Arduino 2.0의 RFID 제어 클래스.
 *
 * 1.0 (`RC522Controller` + `RfidScanner`)을 다음과 같이 재설계:
 *
 *  1) 섹터-캐싱 인증
 *     동일 섹터 내 여러 블록을 연속 쓸 때 인증을 1회만 수행한다.
 *     `WriteBlocks()`는 같은 섹터에서 인증 1회 → N블록 쓰기로 동작.
 *     1.0은 블록마다 매번 PCD_Authenticate를 호출해 7블록 쓰기에 7회 인증이 발생.
 *
 *  2) 안전한 종료
 *     `EndSession()` 단일 진입점에서 PICC_HaltA → PCD_StopCrypto1 보장.
 *     Read/Write 오류 경로도 캐시를 무효화해 다음 인증이 깨끗하게 시작.
 *
 *  3) 트레일러/제조사 블록 보호
 *     `Write/Clear` 진입 시 IsSectorTrailer/IsManufacturerBlock 검사.
 *     트레일러는 InstallTraceQKeys/RestoreFactoryKeys 가 바꾼다(WriteTrailer 는 호출자 0).
 *
 *  4) 쓰기 검증 + 재시도
 *     TRACEQ_RFID_VERIFY_WRITES=1 이면 쓰기 후 같은 블록을 read-back 비교.
 *     실패 시 TRACEQ_RFID_MAX_WRITE_RETRIES 까지 재시도.
 *
 *  5) 태그 발급 키 전환
 *     InstallTraceQKeys/RestoreFactoryKeys — 1.0의 SetAccessMethod/
 *     InitAccessMethod와 동일 절차(공장 FF 키 ↔ TraceQ KEY_B).
 *     (참고: 한때 SPI 8 MHz 상향을 선언했으나 MFRC522 1.2.0에는 해당 매크로가
 *     없어 무효였음 — SPI는 라이브러리 기본 속도로 동작한다.)
 *
 *  6) 비-블로킹 초기화
 *     pcd_reset은 micros() 기반 타임아웃(150ms).
 *
 *  7) 1K/4K 자동 감지
 *     PICC_TYPE_MIFARE_1K 와 _4K 모두 허용.
 */
class RfidController
{
public:
    enum class TagStatus : uint8_t
    {
        NotYetConnected,
        Connected,    // 새 태그 진입
        KeepAlive,    // 동일 태그 유지
        Disconnected, // 태그 이탈
        Invalid,      // UID 못 읽거나 지원 안 함
    };

    RfidController();

    /// 한 번만 호출. (1.0은 loop마다 호출했지만 불필요.)
    void Initialize();

    /// 통신 상태 검사. 0x00/0xFF면 끊긴 것으로 간주.
    bool IsAlive();

    /// 통신 끊김이 감지된 이후 복구. 외부 루프에서 IsAlive() 결과로 호출.
    void Reinitialize();

    /// 새 태그 감지 (1.0의 IsNewTagPresent + IsTagPresent 통합).
    /// wakeHalted=true 면 WUPA 로 정지(HALT)된 카드도 깨운다 — 발급 대기 전용.
    TagStatus Poll(bool wakeHalted = false);

    /// 올려 둔 카드도 다음 Poll 에서 새 카드(Connected)로 보게 한다 — 발급 명령이 "먼저 올려 둔 태그"를 잡도록.
    /// (운영 루프가 이미 처리해 정지시킨 TraceQ 태그는 Poll(true) 로 깨워야 잡힌다.)
    void ForgetTag() { mTagPresent = false; mTagPresentPrev = false; mMissCount = 0; }

    /// 현재 캐시된 UID (Poll() 후 Connected/KeepAlive일 때만 유효).
    const MFRC522::Uid &Uid() const { return mMfrc522.uid; }

    /// 단일 블록 읽기. 같은 섹터면 인증 캐시 재사용.
    RfidResult Read(uint8_t block, void *out, uint8_t outSize);

    /// 단일 블록 쓰기. 검증/재시도 포함. 트레일러/제조사 블록 거부.
    RfidResult Write(uint8_t block, const void *value, uint8_t size);

    /**
     * 연속 블록 일괄 쓰기 — 핵심 성능 최적화 경로.
     *
     * `values`는 길이 `count * stride` 바이트의 연속 버퍼.
     * 각 블록 페이로드는 16바이트 (stride 권장 16). stride < 16이면 zero-pad.
     * 같은 섹터 내 블록은 인증 1회로 처리.
     * 중간에 트레일러 블록을 만나면 자동 skip.
     */
    RfidResult WriteBlocks(uint8_t startBlock, uint8_t count,
                           const void *values, uint8_t stride = MIFARE_BLOCK_SIZE);

    /// 블록을 0으로 초기화.
    RfidResult Clear(uint8_t block);

    /// 섹터 전체(데이터 블록만) 0으로 초기화. 트레일러/제조사 블록은 자동 회피.
    RfidResult ClearSector(uint8_t sector);

    /// 전체 태그(섹터 0~15, 트레일러 제외) 0으로 초기화.
    RfidResult ClearTag();

    /**
     * 트레일러 블록 명시적 쓰기. 잘못된 AccessBits는 태그를 영구 잠글 수 있으므로
     * 이 호출은 신중히. 1.0의 SetAccessMethod/InitAccessMethod 대체용.
     */
    RfidResult WriteTrailer(uint8_t trailerBlock, const uint8_t trailerData[16]);

    /**
     * 공장 태그(KEY_A/B = FF×6) → TraceQ 태그: 전 섹터(0~15) 트레일러에
     * [KEY_A=FF 유지, AccessBits g0~g3=3, KEY_B=AccessKey]를 기록.
     * 1.0 `RfidScanner::SetAccessMethod`와 동일 절차 (KEY_A(FF)로 인증).
     * cfg_new_tag type_id 0 경로에서 사용.
     */
    RfidResult InstallTraceQKeys();

    /**
     * TraceQ 태그 → 공장 기본: 전 섹터 트레일러에
     * [KEY_A=FF, AccessBits (0,0,0,1), KEY_B=FF]를 복원.
     * 1.0 `RfidScanner::InitAccessMethod`와 동일 절차 (KEY_B(AccessKey)로 인증).
     * cfg_new_tag type_id 2 경로에서 사용.
     */
    RfidResult RestoreFactoryKeys();

    /// 현재 태그 세션 종료. HaltA + StopCrypto1 + 캐시 무효화.
    void EndSession();

    /// AccessKey 변경 (KEY_B). 기본은 1.0과 동일 값.
    void SetAccessKey(const uint8_t key[6]);

    /// 디버그용: 마지막 MFRC522 상태 코드.
    MFRC522::StatusCode LastStatus() const { return mLastStatus; }

    /// 진단용 레지스터 읽기 (examples/RegisterProbe 전용 — 운영 경로 미사용).
    uint8_t ReadReg(MFRC522::PCD_Register reg) { return mMfrc522.PCD_ReadRegister(reg); }

private:
    bool ensureAuthenticated(uint8_t block);   // 섹터 캐시 활용 인증

    /**
     * 인증 캐시만 무효화 (StopCrypto1 하지 않음) — **세션 내부** 복구용.
     *
     * MIFARE 카드는 인증 후 암호화 상태를 유지하므로, 세션 도중 리더만
     * StopCrypto1 로 평문화하면 이후 모든 인증이 실패한다(실기 확인).
     * 섹터 전환·읽기/쓰기 실패 재시도는 반드시 이것을 쓴다.
     */
    void dropAuthCache();

    /**
     * 리더의 암호화 상태 해제 — **세션 경계** 전용.
     * (EndSession 의 HaltA 뒤, 태그 이탈/교체 감지, 인증 실패, 초기화)
     * 리더가 암호화 상태로 굳으면 이후 REQA 가 전부 실패하므로 무조건 호출한다.
     */
    void stopCrypto();

    void pcdSoftReset();
    RfidResult writeOnce(uint8_t block, const uint8_t buffer[16]);
    RfidResult writeVerified(uint8_t block, const uint8_t buffer[16]);   // 재시도·검증 정본(Write·WriteBlocks 공용)
    bool verifyBlock(uint8_t block, const uint8_t expected[16]);

    MFRC522 mMfrc522;
    MFRC522::MIFARE_Key mAccessKey{};   // KEY_B
    MFRC522::MIFARE_Key mDefaultKey{};  // FFx6 — 새 태그 초기화용

    // 인증 캐시: 같은 섹터 + 같은 태그(UID) 이면 재인증 skip
    bool    mAuthValid{false};
    uint8_t mAuthSector{0xFF};
    uint8_t mAuthUid[10]{};
    uint8_t mAuthUidSize{0};

    // 리더가 실제로 암호화(Crypto1) 상태인지 — 캐시 유효성과 별개로 추적한다.
    // 캐시를 버려도(dropAuthCache) 리더는 암호화 상태로 남아 있어야 nested
    // authentication 이 성립하고, 세션을 끝낼 때는 반드시 해제해야 한다.
    bool    mCryptoOn{false};

    // 태그 상태 추적
    bool mTagPresentPrev{false};
    bool mTagPresent{false};
    uint8_t mMissCount{0};   // 올려 둔 카드의 연속 무응답 횟수(Poll 디바운스)

    bool mInitialized{false};
    MFRC522::StatusCode mLastStatus{MFRC522::STATUS_OK};
};
