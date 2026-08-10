#include "TraceQ_Arduino/avr/AvrUtil.hpp"

void util_buzzer() { util_buzzer(50); }

void util_buzzer(unsigned long ms) { util_buzzer(ms, 1); }

void util_buzzer(unsigned long ms, uint8_t loop)
{
    for (uint8_t i = 0; i < loop; ++i)
    {
        digitalWrite(PIN_BUZZER, HIGH);
        digitalWrite(PIN_LED, HIGH);
        delay(ms);
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(PIN_LED, LOW);
        delay(ms);
    }
}

[[noreturn]] void util_soft_reset()
{
    asm volatile("jmp 0");
    __builtin_unreachable();
}
