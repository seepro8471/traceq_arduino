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
        util_soft_reset();
    }
    }
}

UserInterface::MenuFunction UserInterface::SetRecordAlarmTimeSlot(char deviceType, AlarmOption &option)
{
    // "%02d" 가 3자리 이상(또는 음수)을 출력할 수 있어 여유 확보 (1.0 승계 결함).
    char numberBuffer[8]{};
    const auto isWashingType{deviceType == 'W'};
    const int8_t current = isWashingType ? option.GetTimeSlot1() : option.GetTimeSlot2();
    snprintf(numberBuffer, sizeof(numberBuffer), "%02d", current);
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
        const int edited = str_atoi(line.GetContent());   // 표시한 자릿수 전부(120 이 12 가 되던 것)
        if (edited >= 0 && current != edited)
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
        util_soft_reset();
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
        util_soft_reset();
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
        util_soft_reset();
    }
    }
}
