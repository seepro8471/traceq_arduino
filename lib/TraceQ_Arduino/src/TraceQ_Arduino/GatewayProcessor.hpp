#pragma once

#include "BaseProcessor.hpp"
#include "TraceQ_Arduino/data/Gateway.hpp"
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

class GatewayProcessor : protected BaseProcessor
{
public:
    GatewayProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : BaseProcessor(tag, tagSerial, process, scanner) {}

    bool HasPatientInformation() const { return mHasPatientInformation; }

    /// PC(SeePro/TraceQ_Python)가 G1 로 내려준 본체번호. 미수신이면 -1.
    int16_t GateNumber() const { return mGateNumber; }

    /**
     * \brief 태그에 기록할 본체번호 (2.2.6, 사용자 확정 규칙).
     *
     *  · PC 가 준 번호가 **0 이 아니면**(그리고 상한 안이면) 그 번호를 쓴다.
     *  · PC 가 `0000` 을 주거나 아직 못 받았으면 **설정값(EEPROM)**을 쓴다 — v2.2.31 부터 설정값은
     *    **PC 가 마지막으로 준 번호**다(G1 이 설정값과 다르면 1회 반영). 그래서 재기동 직후 첫 G1 전에도 맞다.
     *
     * 즉 PC 가 이긴다. PC 가 0 을 주면 설정값, 번호를 비우면(`G1;`) 앞서 받은 번호(없으면 설정값)를 쓴다([12차 판정]) —
     * PC 의 0 은 태그에 쓰이지 않는다.
     */
    int effective_number(int fallback) const
    {
        return (mGateNumber > 0) ? static_cast<int>(mGateNumber) : fallback;
    }

    /// 이 버퍼가 게이트웨이 전문인가 — '{'(JSON) 와 G 마커 중 **먼저 오는 쪽**으로 정한다.
    static bool IsGatewayFrame(const char *buffer);
    /// 버퍼에 이 마커가 **필드 경계**로 있는가(맨 앞·';' 뒤·값 없는 G1 뒤·앞 패킷 끝 G5 뒤).
    static bool HasMarker(const char *buffer, const char *marker);
    /// 마지막 레코드 머리는 있는데 **그 뒤에** 꼬리가 없다 = 전문이 덜 왔다.
    /// ★꼬리를 버퍼 전체에서 보면 앞 레코드의 G5 에 속아 뒤 레코드를 기다리지 않는다(CC1 P3-2).
    static bool NeedsMoreBytes(const char *buffer);
    /// PC 전문 처리. `deviceOption` 은 G1 본체번호를 **설정값에도 반영**하기 위해 받는다(사장님 결정 09-28).
    void GatewaySerialEvent(const char *buffer, DefaultRtc &rtc, DeviceOption &deviceOption);
    void GatewayProcess(int deviceNumber, LcdPrinter &printer);
    void GatewayProcessFallback(int deviceNumber, DefaultRtc &rtc, LcdPrinter &printer);

protected:
    bool is_valid(LcdPrinter &printer);

private:
    static size_t find_marker(const char *src, const char *marker, size_t begin);   // 필드 경계의 마커만
    static bool find_string(const char *src, char *dst, size_t dstSize, const char *from, const char *to);
    bool write_patient_info(int deviceNumber);
    bool write_no_patient_info(const Gateway &gateway, const char *stringDateTime);
    bool substring_for_patient(const char *string);
    void substring_for_examination_subject(const char *string);
    void substring_for_local_date_time(char *string);
    static void print_to_allnun(uint8_t sectorTrailer, uint8_t block, unsigned char *buffer);

    /// PC 가 G1 로 준 본체번호(-1 = 아직 못 받음). 전원이 꺼지면 사라진다.
    int16_t mGateNumber{-1};
    LocalDateTime mDateTime{};
    unsigned char mPatientKey[16]{};
    unsigned char mPatientName[16]{};
    unsigned char mExaminationSubject[16]{};
    unsigned char mExaminationSubject2[16]{};
    unsigned char mExaminationSubject3[16]{};
    bool mHasPatientInformation{false};
};
