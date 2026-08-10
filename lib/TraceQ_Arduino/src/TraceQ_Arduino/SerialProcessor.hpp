#pragma once

#include <ArduinoJson.h>

#include "BaseProcessor.hpp"
#include "TraceQ_Arduino/data/Company.hpp"
#include "TraceQ_Arduino/data/System.hpp"
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/data/nvm/DisinfectionOption.hpp"
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

/**
 * SerialProcessor 2.1 — 1.0(=1.4.1) 대비 변경점:
 *
 *  - 수신은 1.0과 동일하게 raw(프레이밍 없음) + STX·JSON 혼용을 수용한다.
 *    현장 PC(SeePro/TraceQ_Python/구 TraceQ Desktop)는 `Z`/`G1..G5`/`C·M·S`를
 *    STX 없이 raw cp949로 보내므로, STX 프레임만 받는 방식(2.0 초기 ReadFrame)은
 *    현장과 정면 충돌해 폐기했다. serialEvent가 `Serial.readBytes(buf, 511)`로
 *    읽되 마지막 1바이트를 남겨 NUL 종료를 보장한다(1.0의 512B 만재 시 NUL
 *    미보장 OOB를 수정).
 *
 *  - legacy 프로토콜(1.0 SerialProcessor_legacy.cpp) 전 분기 이식:
 *    'Z' 인증, C/M/S 태그 발급, legacy_loop_process(PSOk/Z 핸드셰이크 +
 *    B; C; S; G; W; 섹터 덤프 + "Ok!").
 *
 *  - 모든 substring/검색이 dst 크기 명시(`str_substring_safe`)로만 호출.
 *
 *  - JSON 파싱은 fixed `kFrameBuffer` (512 byte) 안에서만 동작 — 1.0의 `char json[512]`
 *    + `str_substring_c_range` 콤보 대신 ArduinoJson에 frame buffer를 직접 넘김.
 *
 *  - cfg_new_tag type_id 0/2의 키 전환(공장↔TraceQ)을 RfidController의
 *    InstallTraceQKeys/RestoreFactoryKeys로 완전 지원 (2.0에서 미구현이던 것).
 */
class SerialProcessor : public BaseProcessor
{
public:
    static constexpr size_t kFrameBufferSize{512};

    enum class ProcessKind : uint8_t
    {
        NotJson = 0,
        DeserializeError,
        NewTag,
        GetConfig,
        SetConfig,
        SetDateTime,
        Unknown,
    };

    SerialProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : BaseProcessor(tag, tagSerial, process, scanner) {}

    bool IsAuthenticated() const { return mIsAuthenticated; }

    /// 스코프 태그 접촉 시 검사자료 업로드 (레거시 단일 경로 — 2.2.0 에서
    /// Latest 갈래 삭제, 사용자 확정).
    void LoopProcess(LcdPrinter &printer);

    /// 길이 검증된 buffer로부터 ProcessKind 판별.
    ProcessKind GetProcessKind(const char *buffer, size_t length, LcdPrinter &printer);

    void NewTag(DefaultRtc &rtc, LcdPrinter &printer);

    void WriteOptionData(AlarmOption &, DeviceOption &, DisinfectionOption &,
                         ManagerOption &, RecordOption &, DefaultRtc &, LcdPrinter &);

    void UpdateOptionData(AlarmOption &, DeviceOption &, DisinfectionOption &,
                          ManagerOption &, RecordOption &, DefaultRtc &, LcdPrinter &);

    void UpdateDateTime(DefaultRtc &rtc, LcdPrinter &printer);

    /// 1.0의 legacy 프로토콜 진입점 (SERVER 타입 전용).
    /// 미인증 상태에선 'Z' 포함 여부로 인증, 인증 후엔 C/M/S 태그 발급.
    void LegacySerialEvent(const char *buffer, size_t length, LcdPrinter &printer);

protected:
    /// 1.0의 legacy 검사자료 업로드 — W/D 완료 검증 → PSOk/Z 핸드셰이크 →
    /// 섹터 5,6[,7,8],14 + B; C; S; G; W; 블록 덤프 + "Ok!".
    bool legacy_loop_process(LcdPrinter &printer);

    /// 1.0의 C/M/S 태그 발급 (4초 대기, 1초 간격 비프).
    void legacy_create_tag(const char *buffer, LcdPrinter &printer);

    bool update_device_option(DeviceOption &deviceOption, DefaultRtc &rtc);
    void update_alarm_option(AlarmOption &alarmOption);
    void update_disinfection_option(DisinfectionOption &disinfectionOption);
    void update_record_option(RecordOption &recordOption);

private:
    /// PSOk 송신 후 550ms(10ms×55) 안에 'Z' 1바이트 수신 대기 (1.0과 동일).
    static bool legacy_is_connected();

    /// 섹터의 데이터 블록들을 legacy_print_block으로 연속 출력.
    void legacy_print_sector(uint8_t sector);

    /// 블록 1개를 "TTBB{32자 hex};" 37자+CRLF로 출력 (읽기 실패 시 0 덤프 — 1.0과 동일).
    void legacy_print_block(uint8_t sectorTrailer, uint8_t block);

    /// buffer[0](C/M/S)에 따라 태그 타입 파싱. 실패 시 -1.
    int legacy_parse_tag(const char *buffer);
    int legacy_parse_manager_tag(const char *buffer);
    int legacy_parse_scope_tag(const char *buffer);

private:
    bool mIsAuthenticated{false};
    unsigned char mLegacyBuffer[MIFARE_BLOCK_SIZE]{};
    StaticJsonDocument<kFrameBufferSize> mDocument;
};
