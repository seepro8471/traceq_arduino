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

    /// 1 = 정상 · 0 = 거부(담당자 없음 — 알림 냄) · -1 = 읽기 실패(알림 냄). 호출자는 <= 0 이면 나가되,
    /// 소독기의 이동 플래그는 0(거부)에만 내린다.
    int8_t is_valid(const RecordOption &recordOption, const ManagerOption &managerOption, LcdPrinter &printer);
    bool write_manager_key(uint8_t addr);
    bool write_manager_name(uint8_t addr);
    bool try_load_manager_data(const ManagerOption &managerOption, bool isEnd, bool disposability, LcdPrinter &printer);
    /// 일회성 담당자 소모 — 시작 커밋이 성공한 뒤에만 부른다.
    void consume_disposability() { mDisposabilityFlag = false; }
    bool hasnt_patient_info(const RecordOption &recordOption);

    /// 더블터치 판정 — 태그에 적힌 시작 시각이 지금과 2초 안이면 "방금 시작한 그 태그" 다.
    /// 태그가 말해 주므로 RAM 에 마지막 시작을 기억할 필요가 없다(재부팅·기록 실패 뒤에도 옳다).
    /// 더블터치 판정. 1 = 시작이 2초 안(재시작) · 0 = 아니다(종료) · -1 = 시작 블록을 못 읽음(판정 불가 — 호출자가
    /// Read Error 로 알리고 아무것도 바꾸지 않는다). 종전엔 못 읽으면 조용히 0 이 되어 1초짜리 종료가 기록됐다(5차 V4).
    int8_t started_just_now(uint8_t startBlock, DefaultRtc &rtc);

    static LocalDateTime add_datetime(const DateTime &current, uint8_t minute, uint8_t seconds);

private:
    void load_manager_data(const ManagerOption &managerOption);

    bool mDisposabilityFlag{false};
};
