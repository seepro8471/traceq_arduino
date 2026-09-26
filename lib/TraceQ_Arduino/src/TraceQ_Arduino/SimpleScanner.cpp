#include "SimpleScanner.hpp"
// [5차 판정 · 재론 금지] READER_MODE 전용(출하 env 미빌드): Read 반환 전부 버림 · 블록 6 중복 읽기 · count 4 에서 56·57 누락 —
//  살릴 때 legacy_print_block 처럼 실패를 세어 알릴 것.

void SimpleScanner::Scan(int tagType, LcdPrinter &printer)
{
    Serial.print(STX);
    Serial.print(F("["));
    switch (tagType)
    {
    case SCOPE_TYPE_TAG:
    {
        for (uint8_t i = 0; i < 7; ++i) print_sector(i);

        Process process{};
        mScanner.Read(SECTOR1_PROCESS, &process, 9);
        const uint8_t count = process.DisinfectionCount;
        if (count >= 2)
        {
            uint8_t offset = 0;
            switch (count)
            {
            case 2: offset = 9;  break;
            case 3: offset = 11; break;
            case 4: offset = 13; break;
            case 5: offset = 15; break;
            default: break;
            }
            for (uint8_t i = 7; i < offset; ++i) print_sector(i);
        }
        if (count <= 2)
        {
            print_block(56);
            print_block(57);
        }
        else if (count == 3)
        {
            print_sector(14);
        }
        print_block(SECTOR15_EXAMINATION_SUBJECT);
        print_block(SECTOR15_EXAMINATION_SUBJECT2);
        print_block(SECTOR15_EXAMINATION_SUBJECT3, true);
        break;
    }
    case MANAGER_TYPE_TAG:
        print_block(SECTOR0_COMPANY);
        print_block(SECTOR0_TAG);
        print_block(SECTOR1_TAG_SERIAL, true);   // 마지막 — 트레일링 콤마 방지
        break;
    case CLEAR_TYPE_TAG:
        print_block(SECTOR0_COMPANY, true);
        break;
    default:
        print_block(SECTOR0_COMPANY, true);      // 마지막 — 트레일링 콤마 방지
        break;
    }
    Serial.print(F("]"));
    Serial.print(ETX);
    printer.Notify(0, 1, 500, F("Read"));
}

void SimpleScanner::print_sector(uint8_t sector)
{
    uint8_t numberOfBlock = 4;
    uint8_t firstBlock    = sector * numberOfBlock;
    if (sector == 0) { numberOfBlock -= 1; firstBlock += 1; }

    const uint8_t offset = numberOfBlock - 2;
    for (uint8_t b = 0; b <= offset; ++b) print_block(firstBlock + b);
}

void SimpleScanner::print_block(uint8_t block, bool end)
{
    memset(mReadBuffer, 0, sizeof(mReadBuffer));
    mScanner.Read(block, mReadBuffer, sizeof(mReadBuffer));
    Serial.print(F("{\"address\":"));
    Serial.print(block);
    Serial.print(F(","));
    Serial.print(F("\"data\" :\""));
    for (uint8_t i = 0; i < 16; ++i)
    {
        if (mReadBuffer[i] < 0x10) Serial.print(F(" 0"));
        else                       Serial.print(F(" "));
        Serial.print(mReadBuffer[i], HEX);
    }
    Serial.print(end ? "\"}" : "\"},");
}
