#pragma once

#include <Arduino.h>
#include "TraceQ_Arduino/data/PinMap.hpp"

void util_buzzer();
void util_buzzer(unsigned long ms);
void util_buzzer(unsigned long ms, uint8_t loop);

/// 거부음 — 삐삐 뒤에 긴 삐 하나(삐삐—삐~~). "다시 대면 되는 실패(짧게 4회)" 와 귀로 갈린다.
void util_buzzer_reject();

/**
 * \brief 소프트 리셋 — 주소 0으로 점프해 프로그램을 처음부터 재시작한다.
 *
 * 1.0은 `void (*reset)() = nullptr` 호출(UB)로 같은 효과를 냈다. 2.0은 명시적
 * jmp 0으로 동일 동작을 정의된 방법으로 수행한다. 주변장치 레지스터는 초기화되지
 * 않으므로(1.0과 동일) setup()이 다시 구성한다. WDT 리셋은 Mega 2560 구형
 * 부트로더에서 부트루프를 만들 수 있어 쓰지 않는다.
 */
[[noreturn]] void util_soft_reset();
