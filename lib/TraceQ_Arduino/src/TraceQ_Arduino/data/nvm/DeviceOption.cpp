#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/data/System.hpp"

void DeviceOption::Upload()
{
    EEPROM.put(mTypeAddr, mType);
    EEPROM.put(mNumberAddr, mNumber);
}

void DeviceOption::Load()
{
    EEPROM.get(mTypeAddr, mType);
    EEPROM.get(mNumberAddr, mNumber);
}

char DeviceOption::GetType() const
{
    EEPROM.get(mTypeAddr, mType);
    return mType;
}

void DeviceOption::SetType(char type)
{
    // 알려진 타입만 받기 — 잘못된 값은 기본값으로 안전 폴백.
    if (type != GATEWAY_TYPE_DEVICE && type != WASHING_TYPE_DEVICE &&
        type != DISINFECTION_TYPE_DEVICE && type != SERVER_TYPE_DEVICE)
    {
        type = WASHING_TYPE_DEVICE;
    }
    EEPROM.put(mTypeAddr, type);
    EEPROM.get(mTypeAddr, mType);
}

int DeviceOption::GetNumber() const
{
    EEPROM.get(mNumberAddr, mNumber);
    return mNumber;
}

void DeviceOption::SetNumber(int number)
{
    if (number < 0) number = 0;
    EEPROM.put(mNumberAddr, number);
    EEPROM.get(mNumberAddr, mNumber);
}
