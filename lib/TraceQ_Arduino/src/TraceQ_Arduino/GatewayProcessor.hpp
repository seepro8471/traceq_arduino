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

    void GatewaySerialEvent(const char *buffer, DefaultRtc &rtc);
    void GatewayProcess(int deviceNumber, LcdPrinter &printer);
    void GatewayProcessFallback(int deviceNumber, DefaultRtc &rtc, LcdPrinter &printer);

protected:
    bool is_valid(LcdPrinter &printer);

private:
    static bool find_string(const char *src, char *dst, size_t dstSize, const char *from, const char *to);
    bool substring_for_patient(const char *string);
    void substring_for_examination_subject(const char *string);
    void substring_for_local_date_time(char *string);
    static void print_to_allnun(uint8_t sectorTrailer, uint8_t block, unsigned char *buffer);

    LocalDateTime mDateTime{};
    unsigned char mPatientKey[16]{};
    unsigned char mPatientName[16]{};
    unsigned char mExaminationSubject[16]{};
    unsigned char mExaminationSubject2[16]{};
    unsigned char mExaminationSubject3[16]{};
    bool mHasPatientInformation{false};
};
