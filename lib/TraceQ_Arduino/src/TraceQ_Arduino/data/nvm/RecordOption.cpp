#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/RecordOption.hpp"

void RecordOption::Upload()
{
    EEPROM.put(mManagerDisposabilityAddr, mManagerDisposability);
    EEPROM.put(mPatientCheckAddr, mPatientCheck);
}

void RecordOption::Load()
{
    EEPROM.get(mManagerDisposabilityAddr, mManagerDisposability);
    EEPROM.get(mPatientCheckAddr, mPatientCheck);
}

bool RecordOption::GetManagerDisposability() const
{
    EEPROM.get(mManagerDisposabilityAddr, mManagerDisposability);
    return mManagerDisposability;
}

void RecordOption::SetManagerDisposability(bool flag)
{
    EEPROM.put(mManagerDisposabilityAddr, flag);
    EEPROM.get(mManagerDisposabilityAddr, mManagerDisposability);
}

bool RecordOption::GetPatientCheck() const
{
    EEPROM.get(mPatientCheckAddr, mPatientCheck);
    return mPatientCheck;
}

void RecordOption::SetPatientCheck(bool flag)
{
    EEPROM.put(mPatientCheckAddr, flag);
    EEPROM.get(mPatientCheckAddr, mPatientCheck);
}
