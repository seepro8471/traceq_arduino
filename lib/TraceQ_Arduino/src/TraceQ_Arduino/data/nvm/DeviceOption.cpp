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
    // EEPROM 손상 등으로 알 수 없는 값이 읽히면 기본값으로 폴백한다.
    // (main 의 타입 분기 default 는 복구 불가능한 정지였다 — 2.2.5)
    if (mType != GATEWAY_TYPE_DEVICE && mType != WASHING_TYPE_DEVICE &&
        mType != DISINFECTION_TYPE_DEVICE && mType != SERVER_TYPE_DEVICE)
    {
        mType = WASHING_TYPE_DEVICE;
    }
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
