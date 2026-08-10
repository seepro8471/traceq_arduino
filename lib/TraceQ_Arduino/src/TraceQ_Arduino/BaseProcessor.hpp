#pragma once

#include "TraceQ_Arduino/data/BlockMap.hpp"
#include "TraceQ_Arduino/data/Process.hpp"
#include "TraceQ_Arduino/data/Tag.hpp"
#include "TraceQ_Arduino/rfid/RfidController.hpp"
#include "TraceQ_Arduino/avr/AvrUtil.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

/**
 * 모든 Processor의 추상 부모. 2.0에서는 RfidScanner 대신 RfidController를 사용.
 */
class BaseProcessor
{
protected:
    BaseProcessor(Tag &tag, TagSerial &tagSerial, Process &process, RfidController &scanner)
        : mCachedTag(tag), mCachedTagSerial(tagSerial),
          mCachedProcess(process), mScanner(scanner) {}

    static void complete_delay();

    bool print_tag_number(LcdPrinter &printer);
    bool read_tag();
    bool read_tag_serial();
    bool read_process();
    bool write_process();

protected:
    Tag &mCachedTag;
    TagSerial &mCachedTagSerial;
    Process &mCachedProcess;
    RfidController &mScanner;
};
