#pragma once

#include "BaseProcessor.hpp"
#include "TraceQ_Arduino/data/WashingRecord.hpp"
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"
#include "TraceQ_Arduino/module/rtc/DefaultRtc.hpp"

class RecordProcessor : public BaseProcessor
{
public:
    void SaveManagerData(const RecordOption &recordOption, ManagerOption &managerOption, LcdPrinter &printer);

protected:
    RecordProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : BaseProcessor(tag, tagSerial, process, scanner) {}

    bool is_valid(const RecordOption &recordOption, const ManagerOption &managerOption, LcdPrinter &printer);
    bool write_manager_key(uint8_t addr);
    bool write_manager_name(uint8_t addr);
    bool try_load_manager_data(const ManagerOption &managerOption, bool isEnd, bool disposability, LcdPrinter &printer);
    /// 일회성 담당자 소모 — 시작 커밋이 성공한 뒤에만 부른다.
    void consume_disposability() { mDisposabilityFlag = false; }
    bool hasnt_patient_info(const RecordOption &recordOption);

    /// 더블터치 판정 — 태그에 적힌 시작 시각이 지금과 2초 안이면 "방금 시작한 그 태그" 다.
    /// 태그가 말해 주므로 RAM 에 마지막 시작을 기억할 필요가 없다(재부팅·기록 실패 뒤에도 옳다).
    bool started_just_now(uint8_t startBlock, DefaultRtc &rtc);

    static LocalDateTime add_datetime(const DateTime &current, uint8_t minute, uint8_t seconds);

private:
    void load_manager_data(const ManagerOption &managerOption);

    bool mDisposabilityFlag{false};
};
