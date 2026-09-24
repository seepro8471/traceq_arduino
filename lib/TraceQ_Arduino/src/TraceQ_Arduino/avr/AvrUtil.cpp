#include "TraceQ_Arduino/avr/AvrUtil.hpp"

void util_buzzer() { util_buzzer(50); }

void util_buzzer(unsigned long ms) { util_buzzer(ms, 1); }

// 리듬을 바꾸는 것이 유일한 구별 수단이다(부저는 한 가지 소리만 낸다) — 사장님 09-23.
void util_buzzer_reject() { util_buzzer(60, 2); util_buzzer(600, 1); }

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
