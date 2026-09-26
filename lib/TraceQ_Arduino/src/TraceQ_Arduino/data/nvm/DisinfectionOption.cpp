#include <Arduino.h>
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
    EEPROM.put(mClearPendingAddr, mClearPending);
}

void DisinfectionOption::Load()
{
    EEPROM.get(mCountAddr, mCount);
    EEPROM.get(mMaximumCountAddr, mMaximumCount);
    EEPROM.get(mSimultaneousDelayAddr, mSimultaneousDelay);
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
    EEPROM.get(mClearCountAddr, mClearCount);
    EEPROM.get(mClearDateTimeAddr, mClearDateTime);
    EEPROM.get(mClearPendingAddr, mClearPending);
}

int  DisinfectionOption::GetCount() const
{
    EEPROM.get(mCountAddr, mCount);
    return mCount < 0 ? 0 : mCount;   // 세터 범위 밖 값은 돌려주지 않는다
}
void DisinfectionOption::SetCount(int count)
{
    if (count < 0) count = 0;
    EEPROM.put(mCountAddr, count);
    EEPROM.get(mCountAddr, mCount);
}
void DisinfectionOption::IncrementCount()
{
    // 32767 에서 +1 은 int16 이 돌아 0 이 되고(SetCount 가 음수를 0 으로) "MaxCount Over" 가 풀렸다 → 상한에서 멈춘다(X2)
    const int count = GetCount();
    if (count < INT16_MAX) SetCount(count + 1);
}

int  DisinfectionOption::GetMaximumCount() const
{
    EEPROM.get(mMaximumCountAddr, mMaximumCount);
    return mMaximumCount < 0 ? 0 : mMaximumCount;
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
    return static_cast<uint8_t>(constrain(mSimultaneousDelay, 0, 120));
}
void DisinfectionOption::SetSimultaneousDisinfectionDelay(int delay)
{
    // 동시소독 판정에서 int8_t 로 캐스팅되므로 127 을 넘으면 음수가 되어
    // 시간창이 무효화된다 — JSON(df_sim_delay) 경로 방어 (2.2.5).
    const uint8_t v = static_cast<uint8_t>(constrain(delay, 0, 120));
    EEPROM.put(mSimultaneousDelayAddr, v);
    EEPROM.get(mSimultaneousDelayAddr, mSimultaneousDelay);
}

uint8_t DisinfectionOption::GetSimultaneousDisinfectionSlot() const
{
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
    return static_cast<uint8_t>(constrain(mSimultaneousSlot, 0, 2));
}
void DisinfectionOption::SetSimultaneousDisinfectionSlot(int range)
{
    const uint8_t v = static_cast<uint8_t>(constrain(range, 0, 2));
    EEPROM.put(mSimultaneousSlotAddr, v);
    EEPROM.get(mSimultaneousSlotAddr, mSimultaneousSlot);
}

int  DisinfectionOption::GetClearCount() const
{
    EEPROM.get(mClearCountAddr, mClearCount);
    return mClearCount < 0 ? 0 : mClearCount;
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
    return dt.Date.Year == 0 && dt.Date.Month == 0 && dt.Date.Day == 0;   // 합이 0 인 손상값을 '빔' 으로 오판하지 않게
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

uint8_t DisinfectionOption::GetClearPending() const
{
    EEPROM.get(mClearPendingAddr, mClearPending);
    return mClearPending;
}
void DisinfectionOption::SetClearPending(uint8_t kind)
{
    EEPROM.put(mClearPendingAddr, kind);
    EEPROM.get(mClearPendingAddr, mClearPending);
}

void DisinfectionOption::ApplyPendingClear(const LocalDateTime &now)
{
    // [5차 판정 · 재론 금지] 날짜를 쓴 뒤 미룸을 지우기 전에 전원이 끊기면 다음 부팅이 다시 적용해
    //  교환일이 '재부팅 시각' 이 된다(Y2 P3-3). 순서를 바꾸면 날짜가 아예 안 남으니 이쪽이 최선이다.
    const uint8_t kind = GetClearPending();
    if (kind == kPendingNone) return;
    SetClearDateTime(kind == kPendingDefault ? OneMonthBefore(now) : now);
    SetClearPending(kPendingNone);
}

LocalDateTime DisinfectionOption::OneMonthBefore(LocalDateTime t)
{
    // 말일이 짧은 달로 넘어가면 그 달 말일로(3/31 → 2/28, 윤년 2/29).
    uint16_t year = t.Date.Year;
    uint8_t month = t.Date.Month;
    if (month <= 1) { month = 12; year -= 1; }
    else            { month -= 1; }
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint8_t maxDay = (month >= 1 && month <= 12) ? kDays[month - 1] : 28;
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) maxDay = 29;
    if (t.Date.Day > maxDay) t.Date.Day = maxDay;
    t.Date.Year  = year;
    t.Date.Month = month;
    return t;
}
