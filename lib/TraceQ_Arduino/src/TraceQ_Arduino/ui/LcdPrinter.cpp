#include "LcdPrinter.hpp"

void LcdPrinter::Info(const uint8_t col, const uint8_t row, const __FlashStringHelper *string)
{
    mLcd.setCursor(col, row);
    mLcd.print(string);
}

void LcdPrinter::InfoForWhile(const uint8_t col, const uint8_t row, const unsigned long ms,
                              const __FlashStringHelper *string)
{
    clear_line(row);
    // set cursor and print
    mLcd.setCursor(col, row);
    mLcd.print(string);
    // delay
    delay(ms);
    // clear
    clear_line(row);
}

void LcdPrinter::Info_cstr(uint8_t col, uint8_t row, const char *string)
{
    mLcd.setCursor(col, row);
    mLcd.print(string);
}

void LcdPrinter::InfoForWhile_cstr(uint8_t col, uint8_t row, unsigned long ms, const char *string)
{
    clear_line(row);
    // set cursor and print
    mLcd.setCursor(col, row);
    mLcd.print(string);
    // delay
    delay(ms);
    // clear
    clear_line(row);
}

void LcdPrinter::Notify(uint8_t col, uint8_t row, unsigned long ms, const __FlashStringHelper *string)
{
    print_and_notify(col, row, ms, 1, string);
}

void LcdPrinter::Notify_cstr(uint8_t col, uint8_t row, unsigned long ms, const char *string)
{
    print_and_notify_cstr(col, row, ms, 1, string);
}

void LcdPrinter::Warning(const uint8_t col, const uint8_t row, const __FlashStringHelper *string)
{
    print_and_notify(col, row, 40, 4, string);
}

void LcdPrinter::Warning_cstr(uint8_t col, uint8_t row, const char *string)
{
    print_and_notify_cstr(col, row, 40, 4, string);
}

void LcdPrinter::CustomWarning(const uint8_t col, const uint8_t row, const unsigned long ms,
                               const uint8_t count, const __FlashStringHelper *string)
{
    print_and_notify(col, row, ms, count, string);
}

void LcdPrinter::CustomWarning_cstr(const uint8_t col, const uint8_t row, const unsigned long ms,
                                    const uint8_t count, const char *string)
{
    print_and_notify_cstr(col, row, ms, count, string);
}

void LcdPrinter::Debug(const uint8_t col, const uint8_t row, const __FlashStringHelper *string)
{
    // print to serial
    Serial.println(string);
    Warning(col, row, string);
}

void LcdPrinter::Debug_cstr(uint8_t col, uint8_t row, const char *string)
{
    // print to serial
    Serial.println(string);
    Warning_cstr(col, row, string);
}

void LcdPrinter::Reject(const uint8_t col, const uint8_t row, const __FlashStringHelper *string)
{
    clear_line(row);
    mLcd.setCursor(col, row);
    mLcd.print(string);
    util_buzzer_reject();
    clear_line(row);
}

void LcdPrinter::RejectDebug(const uint8_t col, const uint8_t row, const __FlashStringHelper *string)
{
    // 시리얼 에코는 PC 가 음성·화면 안내에 쓴다 — 문구를 바꾸면 안 된다.
    Serial.println(string);
    Reject(col, row, string);
}

void LcdPrinter::CustomDebug(const uint8_t col, const uint8_t row, const unsigned long ms,
                             const uint8_t count, const __FlashStringHelper *string)
{
    // print to serial
    Serial.println(string);
    CustomWarning(col, row, ms, count, string);
}

void LcdPrinter::CustomDebug_cstr(const uint8_t col, const uint8_t row, const unsigned long ms,
                                  const uint8_t count, const char *string)
{
    // print to serial
    Serial.println(string);
    CustomWarning_cstr(col, row, ms, count, string);
}

void LcdPrinter::clear_line(uint8_t row)
{
    // 20x4 LCD 한 행 전체(20칸)를 지운다. (1.0은 19칸만 지워 마지막 열 잔상 발생)
    mLcd.setCursor(0, row);
    mLcd.print(F("                    "));
}

void LcdPrinter::print_and_blink(const uint8_t col, const uint8_t row, const uint8_t count,
                                 const __FlashStringHelper *string)
{
    clear_line(row);
    // print
    mLcd.setCursor(col, row);
    mLcd.print(string);
    // blink
    for (uint8_t i = 0; i < count; ++i)
    {
        digitalWrite(PIN_BUZZER, HIGH);
        digitalWrite(PIN_LED, HIGH);
        delay(40);

        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_LED, LOW);
        delay(40);
    }
    // clear
    clear_line(row);
}

void LcdPrinter::print_and_notify(const uint8_t col, const uint8_t row, const unsigned long ms,
                                  const uint8_t count, const __FlashStringHelper *string)
{
    clear_line(row);
    // print
    mLcd.setCursor(col, row);
    mLcd.print(string);
    // notify
    util_buzzer(ms, count);
    // clear
    clear_line(row);
}

void LcdPrinter::print_and_notify_cstr(const uint8_t col, const uint8_t row, const unsigned long ms,
                                       const uint8_t count, const char *string)
{
    clear_line(row);
    // print
    mLcd.setCursor(col, row);
    mLcd.print(string);
    // notify
    util_buzzer(ms, count);
    // clear
    clear_line(row);
}

void LcdPrinter::print_line(const Line &line)
{
    mLcd.setCursor(line.GetCol(), line.GetRow());
    mLcd.print(line.GetContent());
}

void LcdPrinter::print_screen(const Screen &screen)
{
    print_sceen_by_position(screen, 0, screen.GetLineCount() - 1);
}

void LcdPrinter::print_sceen_by_position(const Screen &screen, uint8_t beggin, uint8_t end)
{
    if (beggin > end)
    {
        const uint8_t temp = beggin;
        beggin = end;
        end = temp;
    }
    const uint8_t lineCount = screen.GetLineCount() - 1;
    if (end > lineCount)
    {
        end = lineCount;
    }
    for (uint8_t i = beggin; i <= end; ++i)
    {
        print_line(screen[i]);
    }
}
