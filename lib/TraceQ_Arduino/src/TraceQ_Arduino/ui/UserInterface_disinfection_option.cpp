#include "UserInterface.hpp"

UserInterface::MenuFunction UserInterface::SetDisinfectionMaximumCount(DisinfectionOption &option)
{
    char numberBuffer[3]{};
    const auto current{option.GetMaximumCount()};
    sprintf(numberBuffer, "%02d", current);
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
        const auto edited = str_atoi_range(line.GetContent(), 0, 1);
        if (current != edited)
        {
            option.SetMaximumCount(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDisinfectionGroupDelay(DisinfectionOption &option)
{
    char numberBuffer[3]{};
    const uint8_t current{option.GetSimultaneousDisinfectionDelay()};
    sprintf(numberBuffer, "%02d", current);
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
        const uint8_t edited = str_atoi_range(line.GetContent(), 0, 1);
        if (current != edited)
        {
            option.SetSimultaneousDisinfectionDelay(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetDisinfectionRange(DisinfectionOption &option)
{
    char numberBuffer[3]{};
    const auto current{option.GetSimultaneousDisinfectionSlot()};
    sprintf(numberBuffer, "%02d", current);
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
        const uint8_t edited = str_atoi_range(line.GetContent(), 0, 1);
        if (current != edited)
        {
            option.SetSimultaneousDisinfectionSlot(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}
