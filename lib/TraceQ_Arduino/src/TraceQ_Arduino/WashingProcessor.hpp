#pragma once

#include "RecordProcessor.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

class WashingProcessor : public RecordProcessor
{
public:
    using RecordProcessor::ResetDisposability;
    WashingProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : RecordProcessor(tag, tagSerial, process, scanner) {}

    void WashingProcess(int deviceNumber, const AlarmOption &alarmOption, const ManagerOption &managerOption,
                        const RecordOption &recordOption, DefaultRtc &rtc, LcdPrinter &printer);

protected:
    void update_process(int deviceNumber);
    /// \return 커밋(Process 기록)까지 성공했는가 — false 면 커밋이 확인되지 않았다(앞선 잔재·표지·환자 블록 소거와
    ///         기록은 이미 됐을 수 있고, 확인 읽기만 실패했으면 커밋도 태그에 있다).
    /// \param isRestart 더블터치(10초 창)로 "시작"이 재실행된 경우 — 잔재 판정·지난 주기 표지 선행 소거·환자 블록
    ///                  소거를 모두 건너뛴다(이미 이번 주기 커밋이 지웠고, 그 커밋을 다시 내리면 찢길 때 사라진다).
    bool washing_start(int deviceNumber, const AlarmOption &alarmOption, DefaultRtc &rtc, bool isRestart);
    bool washing_end(WashingRecord &record);
};
