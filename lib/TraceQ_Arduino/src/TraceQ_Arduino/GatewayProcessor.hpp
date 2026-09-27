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

    /// 이 버퍼가 게이트웨이 전문인가 — 머리글자가 'G' 이거나 **필드 경계에 G1·G2 마커**가 있다.
    /// ★머리글자만 보면 앞에 한 바이트(세척관리 30초 keepalive 'Z')만 붙어도 패킷을 통째로 버렸다(AA2 P1-1).
    ///   머리글자 'G' 도 그대로 둔다 — 머리를 잃은 조각(G3…)도 들어와 묵은 환자정보를 지워야 한다.
    ///   앞에 바이트가 붙으면 첫 G1 은 필드 경계가 아니라 안 걸리고 **G2** 가 걸린다(세 PC 전부 G2 를 보낸다).
    static bool HasGatewayData(const char *buffer);
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
