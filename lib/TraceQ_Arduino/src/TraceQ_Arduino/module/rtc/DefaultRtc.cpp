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

void DefaultRtc::FromString(const char *string)
{
    const auto date = DateTime(string);
    if (date.isValid())
    {
        SetDateTime(date);
    }
}

void DefaultRtc::FromInternalString(char *string, const DateTime &dateTime, DefaultRtc::Format format)
{
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
        // set and break
        SetDateTime({dateTime.year(), dateTime.month(), dateTime.day(), hour, minute, second});
        break;
    }
    case Format::DateTime:
    {
        year = str_atoi_range(string, 0, 1) + 2000;
        month = str_atoi_range(string, 2, 3);
        day = str_atoi_range(string, 4, 5);
        hour = str_atoi_range(string, 0, 1);
        minute = str_atoi_range(string, 2, 3);
        second = str_atoi_range(string, 4, 5);
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
