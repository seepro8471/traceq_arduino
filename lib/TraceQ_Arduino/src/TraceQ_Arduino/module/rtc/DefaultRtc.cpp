#include "DefaultRtc.hpp"

LocalDateTime DefaultRtc::GetCurrentLocalDateTime()
{
    const auto current{GetCurrentDateTime()};
    return LocalDateTime{
        LocalDate{
            current.year(), current.month(), current.day()},
        LocalTime{
            current.hour(), current.minute(), current.second()}};
}

DateTime DefaultRtc::ToDateTime(const LocalDateTime &dateTime)
{
    return DateTime{
        dateTime.Date.Year,
        dateTime.Date.Month,
        dateTime.Date.Day,

        dateTime.Time.Hour,
        dateTime.Time.Minute,
        dateTime.Time.Second};
}

LocalDateTime DefaultRtc::ToLocalDateTime(const DateTime &dateTime)
{
    return LocalDateTime(
        LocalDate{dateTime.year(), dateTime.month(), dateTime.day()},
        LocalTime{dateTime.hour(), dateTime.minute(), dateTime.second()});
}

DateTime DefaultRtc::AddTimeSpan(const DateTime &dateTime, int8_t minute, int8_t second)
{
    return dateTime + TimeSpan{0, 0, minute, second};
}

char *DefaultRtc::ToString(char *outBuffer)
{
    return mRtc.now().toString(outBuffer);
}

char *DefaultRtc::ToString(const LocalDateTime &dateTime, char *outBuffer)
{
    return ToDateTime(dateTime).toString(outBuffer);
}

char *DefaultRtc::ToInternalString(char *outBuffer)
{
    return ToInternalString(outBuffer, Format::DateTime);
}

char *DefaultRtc::ToInternalString(char *outBuffer, DefaultRtc::Format format)
{
    const auto now = mRtc.now();
    switch (format)
    {
    case Format::Date:
    {
        // buffer size >= 8
        sprintf(outBuffer, "%02d%02d%02d",
                now.year() - 2000, now.month(), now.day());
        break;
    }
    case Format::Time:
    {
        // buffer size >= 8
        sprintf(outBuffer, "%02d%02d%02d",
                now.hour(), now.minute(), now.second());
        break;
    }
    case Format::DateTime:
    {
        // buffer size >= 20
        sprintf(outBuffer, "%04d/%02d/%02d %02d:%02d:%02d",
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
        break;
    }
    default:
    {
        // bug
        break;
    }
    }
    return outBuffer;
}

// [5차 판정 · 재론 금지] 연도를 2자리로 읽어 "1999-…" 가 2099 — 세 PC 는 20xx 만 보낸다.
void DefaultRtc::FromString(const char *string)
{
    const auto date = DateTime(string);
    if (date.isValid())
    {
        SetDateTime(date);
    }
}

void DefaultRtc::FromInternalString(char *string, const DateTime & /*unused*/, DefaultRtc::Format format)
{
    // ★빠진 절반(날짜 편집이면 시·분·초)은 **저장하는 지금** 의 시계에서 가져온다. 종전엔 메뉴 진입 때
    //  스냅샷을 써서, 편집에 걸린 시간만큼 시계가 되돌아갔다(5차 A2·E 독립 확인).
    const DateTime dateTime = GetCurrentDateTime();
    // date
    uint16_t year;
    uint8_t month;
    uint8_t day;
    // time
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    // by format
    switch (format)
    {
    case Format::Date:
    {
        year = str_atoi_range(string, 0, 1) + 2000;
        month = str_atoi_range(string, 2, 3);
        day = str_atoi_range(string, 4, 5);
        // set and break
        SetDateTime({year, month, day, dateTime.hour(), dateTime.minute(), dateTime.second()});
        break;
    }
    case Format::Time:
    {
        hour = str_atoi_range(string, 0, 1);
        minute = str_atoi_range(string, 2, 3);
        second = str_atoi_range(string, 4, 5);
        // 날짜는 '지금' 것 — 편집이 자정을 끼면(23:59 에 들어가 00:00 뒤 저장) 하루가 어긋나므로, 합성 시각이 지금과
        // 12시간 넘게 차이 나면 그쪽 날로 하루 옮긴다(5차 V3 — E1 수정이 만든 새 경계).
        DateTime composed{dateTime.year(), dateTime.month(), dateTime.day(), hour, minute, second};
        const int32_t diff = (composed - dateTime).totalseconds();
        if (diff > 12L * 3600) composed = composed - TimeSpan(1, 0, 0, 0);
        else if (diff < -12L * 3600) composed = composed + TimeSpan(1, 0, 0, 0);
        SetDateTime(composed);
        break;
    }
    case Format::DateTime:
    {
        year = str_atoi_range(string, 0, 1) + 2000;
        month = str_atoi_range(string, 2, 3);
        day = str_atoi_range(string, 4, 5);
        hour = str_atoi_range(string, 6, 7);     // 호출자 없음(죽은 갈래) — 오프셋만 바로잡아 둔다
        minute = str_atoi_range(string, 8, 9);
        second = str_atoi_range(string, 10, 11);
        // set and break
        SetDateTime({year, month, day, hour, minute, second});
        break;
    }
    default:
    {
        // bug
        break;
    }
    }
}
