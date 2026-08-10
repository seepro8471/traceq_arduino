#pragma once

#include "TraceQ_Arduino/data/BlockMap.hpp"
#include "TraceQ_Arduino/data/Company.hpp"
#include "TraceQ_Arduino/data/Process.hpp"
#include "TraceQ_Arduino/data/System.hpp"
#include "TraceQ_Arduino/rfid/RfidController.hpp"
#include "TraceQ_Arduino/ui/LcdPrinter.hpp"

class SimpleScanner
{
public:
    explicit SimpleScanner(RfidController &scanner) : mScanner(scanner) {}

    void Scan(int tagType, LcdPrinter &printer);

protected:
    void print_sector(uint8_t sector);
    void print_block(uint8_t block, bool end = false);

    RfidController &mScanner;
private:
    unsigned char mReadBuffer[MIFARE_BLOCK_SIZE]{};
};
