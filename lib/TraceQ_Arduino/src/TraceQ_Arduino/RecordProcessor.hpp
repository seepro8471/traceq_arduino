#pragma once

#include "BaseProcessor.hpp"
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
    bool hasnt_patient_info(const RecordOption &recordOption);

    static LocalDateTime add_datetime(const DateTime &current, uint8_t minute, uint8_t seconds);

private:
    void load_manager_data(const ManagerOption &managerOption);

    bool mDisposabilityFlag{false};
};
