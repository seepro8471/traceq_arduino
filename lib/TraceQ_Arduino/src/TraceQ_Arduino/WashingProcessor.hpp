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
    /// \return 커밋(Process 기록)까지 성공했는가 — false 면 태그는 시작 전 상태.
    bool washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc);
    void washing_end(WashingRecord &record);

private:
    // 더블터치 판정용 — 마지막으로 시작한 태그와 그 시각.
    int16_t  mLastStartNo{-1};
    DateTime mLastStartAt{};
};
