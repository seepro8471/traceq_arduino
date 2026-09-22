#include "UserInterface.hpp"

namespace
{
// 1.0은 자리별 0~9만 제한해 월 99·시 77 같은 값이 RTC 로 저장될 수 있었다
// (2.2.1 보강). 저장 시에만 검증 — 조작 방식은 불변.
bool is_valid_date(const char *yymmdd)
{
    const int year  = str_atoi_range(yymmdd, 0, 1);
    const int month = str_atoi_range(yymmdd, 2, 3);
    const int day   = str_atoi_range(yymmdd, 4, 5);
    if (year < 0 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    // 월말·윤년까지 — 2/30·4/31·평년 2/29 를 RTC 에 쓰지 않는다.
    return DateTime(2000 + year, month, day).isValid();
}

bool is_valid_time(const char *hhmmss)
{
    const int hour   = str_atoi_range(hhmmss, 0, 1);
    const int minute = str_atoi_range(hhmmss, 2, 3);
    const int second = str_atoi_range(hhmmss, 4, 5);
    return hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 &&
           second >= 0 && second <= 59;
}
} // namespace

UserInterface::MenuFunction UserInterface::SetDeviceDate(DefaultRtc &rtc)
{
    auto dateTime{rtc.GetCurrentDateTime()};
    // create line
    char dateBuffer[8]{};
    auto line = Line{0, 2, rtc.ToInternalString(dateBuffer, DefaultRtc::Format::Date)};
    switch (edit_number(line, F("Date")))
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        if (!is_valid_date(line.GetContent()))
        {
            // 저장하지 않고 폐기 — 경고(비프 4회) 후 홈 복귀.
            Warning(0, 2, F("Invalid Date"));
            return MenuFunction::Exit;
        }
        // set
        rtc.FromInternalString(line.GetContent(), dateTime, DefaultRtc::Format::Date);
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDeviceTime(DefaultRtc &rtc)
{
    auto dateTime{rtc.GetCurrentDateTime()};
    // create line
    char timeBuffer[8]{};
    auto line = Line{0, 2, rtc.ToInternalString(timeBuffer, DefaultRtc::Format::Time)};
    switch (edit_number(line, F("Time")))
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        if (!is_valid_time(line.GetContent()))
        {
            // 저장하지 않고 폐기 — 경고(비프 4회) 후 홈 복귀.
            Warning(0, 2, F("Invalid Time"));
            return MenuFunction::Exit;
        }
        // set
        rtc.FromInternalString(line.GetContent(), dateTime, DefaultRtc::Format::Time);
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDeviceType(DeviceOption &option)
{
    const auto current{option.GetType()};
    // title
    char title[9]{};
    sprintf(title, "Type (%c)", current);
    // line
    auto line = Line{0, 0, ""};
    switch (select(line, title, "Gateway", "Washing", "Disinfection", "Server"))
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        const auto edited{line.GetContent()[0]};
        if (current != edited)
        {
            option.SetType(edited);
            // 타입이 변경된 경우 재시작이 필요함. (1.0은 nullptr 함수포인터 호출(UB)로
            // 주소 0 점프를 유도했음 — 명시적 소프트 리셋으로 동일 동작.)
            util_soft_reset();
        }
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDeviceNumber(DeviceOption &option)
{
    // "%02d" 가 3자리 이상을 출력할 수 있어 여유 확보 (1.0 승계 결함).
    char numberBuffer[8]{};
    const auto current{option.GetNumber()};
    snprintf(numberBuffer, sizeof(numberBuffer), "%02d", current);
    // title — "Number (NN)" = 11자+NUL (1.0은 [10]이라 2바이트 스택 오버런).
    char title[12]{};
    snprintf(title, sizeof(title), "Number (%s)", numberBuffer);
    // line
    auto line = Line{0, 2, numberBuffer};
    switch (edit_number(line, title))
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        const int edited = str_atoi(line.GetContent());   // 표시한 자릿수 전부(300 이 44 가 되던 것)
        if (edited >= 0 && current != edited)
        {
            option.SetNumber(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}
