#pragma once

#include "TraceQ_Arduino/module/rtc/BaseRtc.hpp"
#include "TraceQ_Arduino/data/LocalDateTime.hpp"
#include "TraceQ_Arduino/avr/AvrString.hpp"

/**
 * \class DefaultRtc
 * \extends BaseRtc
 * \brief BaseRtc의 기능에 더해 변환 등의 추가 기능을 구현하는 클래스.
 *
 * \since 1.0
 */
class DefaultRtc : public BaseRtc
{

public:
    /**
     * \enum Format
     */
    enum class Format
    {

        Date,

        Time,

        DateTime

    };

    DefaultRtc() = default;

    /**
     * \brief 현재 시간을 기준으로 LocalDateTime을 생성해 반환한다.
     *
     * \details 내부적으로 GetCurrentDateTime() 함수를 호출함.
     *
     * \return 현재 시간을 기준으로 생성된 LocalDateTime
     */
    LocalDateTime GetCurrentLocalDateTime();

    /**
     * \param dateTime LocalDateTime instance
     * \return LocalDateTime을 기준으로 '생성된' DateTime
     */
    static DateTime ToDateTime(const LocalDateTime &dateTime);

    /**
     * \param dateTime DateTiem instance
     * \return DateTime을 기준으로 '생성된' LocalDateTime
     */
    static LocalDateTime ToLocalDateTime(const DateTime &dateTime);

    /**
     * \brief 인자로 전달받은 dateTime에 minute, second을/를 더한 '새로운' DateTime을/를 반환한다.
     *
     * \param dateTime 원본 시간
     * \param minute dateTime에 더할 minute
     * \param second dateTime에 더할 second
     * \return minute, second을/를 더한 '새로운' DateTime
     */
    static DateTime AddTimeSpan(const DateTime &dateTime, int8_t minute, int8_t second);

    /**
     * @brief RTClib의 toString()을 호출해 특정한 형식의 문자열을 생성한다.
     * @param outBuffer `YYYY-MM-DD hh:mm:ss`와 같은 형식을 저장하고 있는 문자열
     * @return 변환 결과가 저장된 outBuffer
     */
    char *ToString(char *outBuffer);

    char *ToString(const LocalDateTime &dateTime, char *outBuffer);

    /**
     * \brief 현재 시간을 문자열로 변환한다.
     *
     * \param outBuffer 변환된 문자열을 저장할 버퍼
     * \return 변환 결과가 저장된 outBuffer
     */
    char *ToInternalString(char *outBuffer);

    /**
     * \brief 현재 시간을 변환 형식에 맞춰 문자열로 변환한다.
     *
     * \param outBuffer 변환된 문자열을 저장할 버퍼
     * \param format 변환 형식
     * \return 변환 결과가 저장된 outBuffer
     */
    char *ToInternalString(char *outBuffer, Format format);

    /**
     * @brief
     * @param string
     */
    void FromString(const char *string);

    /**
     * \brief 인자로 전달받은 문자열을 통해 현재 시간을 변경한다.
     *
     * \details
     *
     * \param string 변경할 시간의 정보가 저장된 문자열
     * \param dateTime 문자열이 일부의 정보만을 가지고 있을 경우 사용된다. 자세한 내용은 details를 참고할 것
     * \param format 문자열이 저장하고 있는 데이터의 형식
     */
    void FromInternalString(char *string, const DateTime &dateTime, Format format);
};
