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
    /// \param isRestart 더블터치 2초 가드로 "시작"이 재실행된 경우 — 지난 주기 표지 선행 소거를
    ///                  건너뛴다(이미 이번 주기 커밋이 지웠고, 그 커밋을 다시 내리면 찢길 때 사라진다).
    bool washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc, bool isRestart);
    bool washing_end(WashingRecord &record);
};
