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
    return mTimeSlot1;
}

void AlarmOption::SetTimeSlot1(int minute)
{
    const int8_t v = static_cast<int8_t>(constrain(minute, 0, 127));
    EEPROM.put(mTimeSlot1Addr, v);
    EEPROM.get(mTimeSlot1Addr, mTimeSlot1);
}

int8_t AlarmOption::GetTimeSlot2() const
{
    EEPROM.get(mTimeSlot2Addr, mTimeSlot2);
    return mTimeSlot2;
}

void AlarmOption::SetTimeSlot2(int minute)
{
    const int8_t v = static_cast<int8_t>(constrain(minute, 0, 127));
    EEPROM.put(mTimeSlot2Addr, v);
    EEPROM.get(mTimeSlot2Addr, mTimeSlot2);
}
