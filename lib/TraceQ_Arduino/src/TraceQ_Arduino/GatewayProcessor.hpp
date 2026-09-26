#pragma once

#include "BaseProcessor.hpp"
#include "TraceQ_Arduino/data/Gateway.hpp"
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
     *  · PC 가 준 번호가 **0 이 아니면** 그 번호를 쓴다.
     *  · PC 가 `0000` 을 주거나 아직 못 받았으면 **기기 자체 번호**를 쓴다.
     *
     * 즉 현장에서 PC 설정을 비워 두면(0000) 기기 메뉴의 번호가 쓰이고,
     * PC 에 번호를 넣으면 그쪽이 이긴다.
     */
    int effective_number(int fallback) const
    {
        return (mGateNumber > 0) ? static_cast<int>(mGateNumber) : fallback;
    }

    void GatewaySerialEvent(const char *buffer, DefaultRtc &rtc);
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
