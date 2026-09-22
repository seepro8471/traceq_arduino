#pragma once

#include <RTClib.h>

#include "TraceQ_Arduino/data/PinMap.hpp"

/**
 * \class BaseRtc
 * \brief RTC_DS3231 클래스를 사용해 현재 시간을 조정하고 알람을 설정할 수 있는 추상 클래스.
 *
 * \since 1.0
 */
class BaseRtc
{

public:
    /**
     * \brief RTC_DS3231 모듈을 초기화한다.
     * \details 자체적으로 알람 기능을 구현하므로 RTC_DS3231 클래스에서 제공하는 알람 기능은 모두 비활성화 시킨다.
     */
    void RtcInitialize();

    DateTime GetCurrentDateTime();

    TimeSpan GetCurrentTimeSpan();

    void SetDateTime(const DateTime &dateTime);

    /// 배터리 방전으로 2026-01-01 에서 시작한 뒤 아직 맞춘 적이 없는가(SetDateTime 이 풀어 준다).
    bool IsUnsynced() const { return mUnsynced; }

    /**
     * \brief 지정된 위치에 설정된 알람이 종료되었는지(fired) 확인하고 parameter useBuzzer의 값에 따라 buzzer를 사용한다.
     *
     * \details 알람이 설정되지 않았다면 아무런 작업도 하지 않는다.
     *
     * \param slot 알람을 설정한 위치
     * \param useBuzzer 이 값이 true이면서 알람이 종료되었다면(fired) buzzer를 사용해 알린다
     */
    void HandleAlarm(uint8_t slot, bool useBuzzer);

    /**
     * \brief 지정된 위치의 알람(DateTime)을/를 TimeSpan으로 만들어 반환한다.
     *
     * \param slot TimeSpan을 가져올 알람의 위치
     * \return 알람의 TimeSpan
     */
    TimeSpan GetAlarmTimeSpan(uint8_t slot);

    /**
     * \brief 지정된 위치에 알람을 설정한다.
     *
     * \param slot 알람을 설정할 위치
     * \param minute 설명 생략
     * \param seconds 설명 생략
     */
    void SetAlarm(uint8_t slot, int8_t minute, int8_t seconds);

    /**
     * \brief 지정된 위치의 알람을 비활성화한다.
     *
     * \param slot 알람을 비활성화 할 위치
     */
    void ClearAlarm(uint8_t slot);

    /**
     * \brief 지정된 위치에 알람이 설정되어있는지 확인한다.
     *
     * \param slot 확인할 위치
     * \return 지정된 위치에 알람이 설정되어있다면 true. otherwise, flase
     */
    bool HasAlarm(uint8_t slot);

protected:
    BaseRtc() = default;

    /**
     * \brief 지정된 위치의 알람의 종료 상태를 확인하고 그 값을 반환한다.
     * \param slot 알람을 설정한 위치
     * \return 알람이 종료되었다면(fired) ture. otherwise, false
     */
    bool is_alarm_fired(uint8_t slot);

    /**
     * \param dateTime TimeSpan을 추출할 DateTime
     * \return dateTime의 TimeSpan
     */
    static TimeSpan get_time_span(const DateTime &dateTime);

protected:
    RTC_DS3231 mRtc{};

private:
    DateTime mAlarmSlot1{};

    /**
     * \brief Slot1에 알람이 설정되어있다면 true. otherwise, false.
     */
    bool mAlarmSlot1Flag{false};

    DateTime mAlarmSlot2{};

    /**
     * \brief Slot2에 알람이 설정되어있다면 true. otherwise, false.
     */
    bool mAlarmSlot2Flag{false};

    bool mUnsynced{false};
};
