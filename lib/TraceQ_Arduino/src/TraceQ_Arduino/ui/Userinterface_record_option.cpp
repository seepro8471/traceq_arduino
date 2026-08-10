#include "UserInterface.hpp"

UserInterface::MenuFunction UserInterface::SetRecordAlarmFlag(AlarmOption &option)
{
    const auto flag{option.GetFlag()};
    // line
    auto line = Line{0, 0, ""};
    // by flag
    const auto eResult = flag ? select(line, F("Alarm Sound (Yes)"), "Yes", "No") : select(line, F("Alarm Sound (No)"), "Yes", "No");
    switch (eResult)
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        const auto edited{line.GetContent()[0] == 'Y'};
        if (flag != edited)
        {
            option.SetFlag(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetRecordAlarmTimeSlot(char deviceType, AlarmOption &option)
{
    char numberBuffer[3]{};
    const auto isWashingType{deviceType == 'W'};
    const int8_t current = isWashingType ? option.GetTimeSlot1() : option.GetTimeSlot2();
    sprintf(numberBuffer, "%02d", current);
    // title — "Alarm Time (NN)" = 15자+NUL (1.0은 [14]라 2바이트 스택 오버런).
    char title[16]{};
    snprintf(title, sizeof(title), "Alarm Time (%s)", numberBuffer);
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
        const auto edited = (int8_t)str_atoi_range(line.GetContent(), 0, 1);
        if (current != edited)
        {
            if (isWashingType)
            {
                option.SetTimeSlot1(edited);
            }
            else
            {
                option.SetTimeSlot2(edited);
            }
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetRecordPatientCheck(RecordOption &option)
{
    const auto flag{option.GetPatientCheck()};
    // line
    auto line = Line{0, 0, ""};
    // by flag
    const auto eResult = flag ? select(line, F("Patient Check (Yes)"), "Yes", "No") : select(line, F("Patient Check (No)"), "Yes", "No");
    switch (eResult)
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        const auto edited{line.GetContent()[0] == 'Y'};
        if (flag != edited)
        {
            option.SetPatientCheck(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetRecordManagerDisposability(RecordOption &option)
{
    const auto flag{option.GetManagerDisposability()};
    // line
    auto line = Line{0, 0, ""};
    // by flag
    const auto eResult = flag ? select(line, F("Manager Check(Yes)"), "Yes", "No") : select(line, F("Manager Check (No)"), "Yes", "No");
    switch (eResult)
    {
    case MenuFunction::Exit:
    {
        return MenuFunction::Exit;
    }
    case MenuFunction::Save:
    {
        const auto edited{line.GetContent()[0] == 'Y'};
        if (flag != edited)
        {
            option.SetManagerDisposability(edited);
        }
        return MenuFunction::Save;
    }
    default:
    {
        abort();
    }
    }
}
