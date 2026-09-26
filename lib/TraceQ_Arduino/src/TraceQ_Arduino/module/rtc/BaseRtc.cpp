#include "BaseRtc.hpp"

#include "TraceQ_Arduino/version.hpp"

void BaseRtc::RtcInitialize()
{
    if (!mRtc.begin())
    {
        // error
    }
    if (mRtc.lostPower())
    {
        // 배터리 방전으로 시각 소실 시 2026-01-01 00:00:00 고정 (2.2.2,
        // 사용자 확정). 1.0은 컴파일 시각으로 맞췄는데 그럴듯해 보여서
        // 시계 이상을 알아채기 어려웠다 — 고정 날짜는 화면만 봐도 배터리
        // 방전임을 알 수 있고, 과거 날짜라 소독기의 RTC 자동 복구
        // (세척 종료+1분)와 비교가 항상 성립해 운용 중 자동 교정된다.
        mRtc.adjust(DateTime(2026, 1, 1, 0, 0, 0));
    }
    // set alarm 1, 2 flag to false
    mRtc.clearAlarm(1);
    mRtc.clearAlarm(2);
    // stop oscillating signals at SQW Pin
    mRtc.writeSqwPinMode(DS3231_OFF);
    // turn off alarm 2
    mRtc.disableAlarm(1);
    mRtc.disableAlarm(2);
}

DateTime BaseRtc::GetCurrentDateTime()
{
    return mRtc.now();
}

TimeSpan BaseRtc::GetCurrentTimeSpan()
{
    return get_time_span(mRtc.now());
}

void BaseRtc::SetDateTime(const DateTime &dateTime)
{
    mRtc.adjust(dateTime);
}

bool BaseRtc::IsUnsynced()
{
    return mRtc.now() < DateTime(TRACEQ_RELEASE_YEAR, TRACEQ_RELEASE_MONTH, TRACEQ_RELEASE_DAY, 0, 0, 0);
}

void BaseRtc::HandleAlarm(const uint8_t slot, bool useBuzzer)
{
    if (slot > 3)
    {
        return;
    }
    if (HasAlarm(slot))
    {
        digitalWrite(PIN_LED, HIGH);
        if (is_alarm_fired(slot))
        {
            ClearAlarm(slot);
            if (useBuzzer)
            {
                for (uint8_t i = 0; i < 4; ++i)
                {
                    digitalWrite(PIN_BUZZER, HIGH);
                    digitalWrite(PIN_LED, HIGH);
                    delay(300);

                    digitalWrite(PIN_BUZZER, LOW);
                    digitalWrite(PIN_LED, LOW);
                    delay(300);
                }
            }
            digitalWrite(PIN_LED, LOW);
        }
    }
    else
    { // HasAlarm(slot)
        digitalWrite(PIN_LED, LOW);
    }
}

TimeSpan BaseRtc::GetAlarmTimeSpan(uint8_t slot)
{
    if (slot > 3)
    {
        return TimeSpan{};
    }
    return get_time_span(slot == 1 ? mAlarmSlot1 : mAlarmSlot2);
}

void BaseRtc::SetAlarm(const uint8_t slot, const int8_t minute, const int8_t seconds)
{
    if (slot > 3)
    {
        return;
    }
    const auto alarm{mRtc.now() + TimeSpan{0, 0, minute, seconds}};
    if (slot == 1)
    {
        mAlarmSlot1 = alarm;
        mAlarmSlot1Flag = true;
    }
    else
    {
        mAlarmSlot2 = alarm;
        mAlarmSlot2Flag = true;
    }
}

void BaseRtc::ClearAlarm(const uint8_t slot)
{
    if (slot == 1)
    {
        mAlarmSlot1 = DateTime{};
        mAlarmSlot1Flag = false;
    }
    else
    {
        mAlarmSlot2 = DateTime{};
        mAlarmSlot2Flag = false;
    }
}

bool BaseRtc::HasAlarm(const uint8_t slot)
{
    return slot == 1 ? mAlarmSlot1Flag : mAlarmSlot2Flag;
}

int32_t BaseRtc::GetAlarmRemainingSeconds(const uint8_t slot)
{
    // ★남은 시간은 **절대 시각 차**로 — GetAlarmTimeSpan 은 하루 안 시각으로 잘라 자정을 넘으면 음수가 됐고,
    //  TimeSpan::minutes() 는 (초/60)%60 이라 60분 넘는 설정에서 나머지만 보였다(표시 전용, 발화는 무관).
    if (slot > 3) return 0;
    const bool flag = slot == 1 ? mAlarmSlot1Flag : mAlarmSlot2Flag;
    if (!flag) return 0;
    const DateTime alarm = slot == 1 ? mAlarmSlot1 : mAlarmSlot2;
    const DateTime now   = mRtc.now();
    if (now >= alarm) return 0;
    return (alarm - now).totalseconds();
}

bool BaseRtc::is_alarm_fired(uint8_t slot)
{
    const auto alarmFlag = slot == 1 ? mAlarmSlot1Flag : mAlarmSlot2Flag;
    if (!alarmFlag)
    {
        return false;
    }
    const auto current = mRtc.now();
    const auto alarm = slot == 1 ? mAlarmSlot1 : mAlarmSlot2;
    // compare
    return current >= alarm;
}

TimeSpan BaseRtc::get_time_span(const DateTime &dateTime)
{
    return TimeSpan{
        0, int8_t(dateTime.hour()), int8_t(dateTime.minute()), int8_t(dateTime.second())};
}
