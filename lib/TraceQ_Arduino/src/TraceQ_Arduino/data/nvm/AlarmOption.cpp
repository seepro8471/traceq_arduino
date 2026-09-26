#include <Arduino.h>
#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/AlarmOption.hpp"

void AlarmOption::Upload()
{
    EEPROM.put(mSoundAddr, mSound);
    EEPROM.put(mTimeSlot1Addr, mTimeSlot1);
    EEPROM.put(mTimeSlot2Addr, mTimeSlot2);
}

void AlarmOption::Load()
{
    EEPROM.get(mSoundAddr, mSound);
    EEPROM.get(mTimeSlot1Addr, mTimeSlot1);
    EEPROM.get(mTimeSlot2Addr, mTimeSlot2);
}

bool AlarmOption::GetFlag() const
{
    EEPROM.get(mSoundAddr, mSound);
    return mSound;
}

void AlarmOption::SetFlag(bool flag)
{
    EEPROM.put(mSoundAddr, flag);
    EEPROM.get(mSoundAddr, mSound);
}

int8_t AlarmOption::GetTimeSlot1() const
{
    EEPROM.get(mTimeSlot1Addr, mTimeSlot1);
    // ★세터가 저장하지 않는 값은 돌려주지 않는다(GetType 폴백과 같은 규칙) — 손상·구판 바이트가 음수면
    //  자동 종료가 시작보다 앞선 시각으로 태그에 찍혔다.
    return static_cast<int8_t>(constrain(mTimeSlot1, 0, 127));
}

void AlarmOption::SetTimeSlot1(int minute)
{
    // [5차 판정 · 재론 금지] 0분은 허용(1.0 동일 — 리더기 메뉴·손 JSON 으로 넣을 수 있다, 설정기 spin 은 1~120) — 즉시 알람·자동 종료=시작 시각.
    const int8_t v = static_cast<int8_t>(constrain(minute, 0, 127));
    EEPROM.put(mTimeSlot1Addr, v);
    EEPROM.get(mTimeSlot1Addr, mTimeSlot1);
}

int8_t AlarmOption::GetTimeSlot2() const
{
    EEPROM.get(mTimeSlot2Addr, mTimeSlot2);
    // ★세터가 저장하지 않는 값은 돌려주지 않는다(GetType 폴백과 같은 규칙) — 손상·구판 바이트가 음수면
    //  자동 종료가 시작보다 앞선 시각으로 태그에 찍혔다.
    return static_cast<int8_t>(constrain(mTimeSlot2, 0, 127));
}

void AlarmOption::SetTimeSlot2(int minute)
{
    const int8_t v = static_cast<int8_t>(constrain(minute, 0, 127));
    EEPROM.put(mTimeSlot2Addr, v);
    EEPROM.get(mTimeSlot2Addr, mTimeSlot2);
}
