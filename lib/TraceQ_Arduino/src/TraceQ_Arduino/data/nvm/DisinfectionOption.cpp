#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/DisinfectionOption.hpp"

void DisinfectionOption::Upload()
{
    EEPROM.put(mCountAddr, mCount);
    EEPROM.put(mMaximumCountAddr, mMaximumCount);
    EEPROM.put(mSimultaneousDelayAddr, mSimultaneousDelay);
    EEPROM.put(mSimultaneousSlotAddr, mSimultaneousSlot);
    EEPROM.put(mClearCountAddr, mClearCount);
    EEPROM.put(mClearDateTimeAddr, mClearDateTime);
}

void DisinfectionOption::Load()
{
    EEPROM.get(mCountAddr, mCount);
    EEPROM.get(mMaximumCountAddr, mMaximumCount);
    EEPROM.get(mSimultaneousDelayAddr, mSimultaneousDelay);
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
    EEPROM.get(mClearCountAddr, mClearCount);
    EEPROM.get(mClearDateTimeAddr, mClearDateTime);
}

int  DisinfectionOption::GetCount() const
{
    EEPROM.get(mCountAddr, mCount);
    return mCount;
}
void DisinfectionOption::SetCount(int count)
{
    if (count < 0) count = 0;
    EEPROM.put(mCountAddr, count);
    EEPROM.get(mCountAddr, mCount);
}
void DisinfectionOption::IncrementCount() { SetCount(GetCount() + 1); }

int  DisinfectionOption::GetMaximumCount() const
{
    EEPROM.get(mMaximumCountAddr, mMaximumCount);
    return mMaximumCount;
}
void DisinfectionOption::SetMaximumCount(int maximumCount)
{
    if (maximumCount < 0) maximumCount = 0;
    EEPROM.put(mMaximumCountAddr, maximumCount);
    EEPROM.get(mMaximumCountAddr, mMaximumCount);
}

uint8_t DisinfectionOption::GetSimultaneousDisinfectionDelay() const
{
    EEPROM.get(mSimultaneousDelayAddr, mSimultaneousDelay);
    return mSimultaneousDelay;
}
void DisinfectionOption::SetSimultaneousDisinfectionDelay(uint8_t delay)
{
    EEPROM.put(mSimultaneousDelayAddr, delay);
    EEPROM.get(mSimultaneousDelayAddr, mSimultaneousDelay);
}

uint8_t DisinfectionOption::GetSimultaneousDisinfectionSlot() const
{
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
    return mSimultaneousSlot;
}
void DisinfectionOption::SetSimultaneousDisinfectionSlot(uint8_t range)
{
    if (range > 2) range = 2;
    EEPROM.put(mSimultaneousSlotAddr, range);
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
}

int  DisinfectionOption::GetClearCount() const
{
    EEPROM.get(mClearCountAddr, mClearCount);
    return mClearCount;
}
void DisinfectionOption::SetClearCount(int clearCount)
{
    if (clearCount < 0) clearCount = 0;
    EEPROM.put(mClearCountAddr, clearCount);
    EEPROM.get(mClearCountAddr, mClearCount);
}
void DisinfectionOption::IncrementClearCount() { SetClearCount(GetClearCount() + 1); }

bool DisinfectionOption::IsClearDateTimeEmpty() const
{
    const auto dt = GetClearDateTime();
    return (dt.Date.Year + dt.Date.Month + dt.Date.Day) == 0;
}

LocalDateTime DisinfectionOption::GetClearDateTime() const
{
    EEPROM.get(mClearDateTimeAddr, mClearDateTime);
    return mClearDateTime;
}

void DisinfectionOption::SetClearDateTime(const LocalDateTime &dateTime)
{
    EEPROM.put(mClearDateTimeAddr, dateTime);
    EEPROM.get(mClearDateTimeAddr, mClearDateTime);
}
