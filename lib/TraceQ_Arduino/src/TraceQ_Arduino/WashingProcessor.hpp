#pragma once

#include "RecordProcessor.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

class WashingProcessor : public RecordProcessor
{
public:
    WashingProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : RecordProcessor(tag, tagSerial, process, scanner) {}

    void WashingProcess(int deviceNumber, const AlarmOption &alarmOption, const ManagerOption &managerOption,
                        const RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer);

protected:
    void update_process(int deviceNumber);
    void washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc);
    void washing_end(WashingRecord &record);
};
