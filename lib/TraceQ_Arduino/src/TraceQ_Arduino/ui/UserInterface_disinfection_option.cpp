#include "UserInterface.hpp"

UserInterface::MenuFunction UserInterface::SetDisinfectionMaximumCount(DisinfectionOption &option)
{
    // int16 최댓값(5자리)+NUL 까지 수용 — JSON 원격 설정으로 100 이상이 되면
    // "%02d" 가 3자리 이상을 출력해 [3] 버퍼를 넘긴다(1.0 승계 결함).
    char numberBuffer[8]{};
    const auto current{option.GetMaximumCount()};
    snprintf(numberBuffer, sizeof(numberBuffer), "%02d", current);
    // title — "Max Count (NN)" = 14자+NUL (1.0은 [13]이라 2바이트 스택 오버런).
    char title[16]{};
    snprintf(title, sizeof(title), "Max Count (%s)", numberBuffer);
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
        // 표시·편집한 자릿수 전부를 읽는다(앞 2자리만 읽어 150 이 15 로 저장되던 것).
        const int edited = str_atoi(line.GetContent());
        if (edited >= 0 && current != edited)
        {
            option.SetMaximumCount(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDisinfectionGroupDelay(DisinfectionOption &option)
{
    // int16 최댓값(5자리)+NUL 까지 수용 — JSON 원격 설정으로 100 이상이 되면
    // "%02d" 가 3자리 이상을 출력해 [3] 버퍼를 넘긴다(1.0 승계 결함).
    char numberBuffer[8]{};
    const uint8_t current{option.GetSimultaneousDisinfectionDelay()};
    snprintf(numberBuffer, sizeof(numberBuffer), "%02d", current);
    // title — "Delay (NN)" = 10자+NUL (1.0은 [9]라 2바이트 스택 오버런).
    char title[12]{};
    snprintf(title, sizeof(title), "Delay (%s)", numberBuffer);
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
        const int edited = str_atoi(line.GetContent());
        if (edited >= 0 && current != edited)
        {
            option.SetSimultaneousDisinfectionDelay(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDisinfectionRange(DisinfectionOption &option)
{
    // int16 최댓값(5자리)+NUL 까지 수용 — JSON 원격 설정으로 100 이상이 되면
    // "%02d" 가 3자리 이상을 출력해 [3] 버퍼를 넘긴다(1.0 승계 결함).
    char numberBuffer[8]{};
    const auto current{option.GetSimultaneousDisinfectionSlot()};
    snprintf(numberBuffer, sizeof(numberBuffer), "%02d", current);
    // title — "Range (NN)" = 10자+NUL (1.0은 [9]라 2바이트 스택 오버런).
    char title[12]{};
    snprintf(title, sizeof(title), "Range (%s)", numberBuffer);
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
        const int edited = str_atoi(line.GetContent());
        if (edited >= 0 && current != edited)
        {
            option.SetSimultaneousDisinfectionSlot(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        util_soft_reset();
    }
    }
}
