#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/ManagerOption.hpp"

void ManagerOption::Upload()
{
    EEPROM.put(mKeyAddr, mKey);
    EEPROM.put(mNameAddr, mName);
    EEPROM.put(mFlagAddr, mFlag);
}

void ManagerOption::Load()
{
    EEPROM.get(mKeyAddr, mKey);
    EEPROM.get(mNameAddr, mName);
    EEPROM.get(mFlagAddr, mFlag);
}

void ManagerOption::GetKey(unsigned char *outBuffer, uint8_t size) const
{
    if (outBuffer == nullptr || size == 0) return;
    if (size > KEY_SIZE) size = KEY_SIZE;
    memset(outBuffer, 0, size);
    EEPROM.get(mKeyAddr, mKey);
    memcpy(outBuffer, mKey, size);
}

void ManagerOption::SetKey(const unsigned char *buffer)
{
    memset(mKey, 0, KEY_SIZE);
    if (buffer == nullptr)
    {
        setManagerFlag(false);
    }
    else
    {
        memcpy(mKey, buffer, KEY_SIZE);
        setManagerFlag(true);
    }
    EEPROM.put(mKeyAddr, mKey);
    EEPROM.get(mKeyAddr, mKey);
}

void ManagerOption::GetName(unsigned char *outBuffer, uint8_t size) const
{
    if (outBuffer == nullptr || size == 0) return;
    if (size > NAME_SIZE) size = NAME_SIZE;
    memset(outBuffer, 0, size);
    EEPROM.get(mNameAddr, mName);
    memcpy(outBuffer, mName, size);
}

void ManagerOption::SetName(const unsigned char *buffer)
{
    memset(mName, 0, NAME_SIZE);
    if (buffer == nullptr)
    {
        setManagerFlag(false);
    }
    else
    {
        memcpy(mName, buffer, NAME_SIZE);
        setManagerFlag(true);
    }
    EEPROM.put(mNameAddr, mName);
    EEPROM.get(mNameAddr, mName);
}

bool ManagerOption::HasData() const
{
    EEPROM.get(mFlagAddr, mFlag);
    return mFlag;
}

void ManagerOption::setManagerFlag(bool flag)
{
    EEPROM.put(mFlagAddr, flag);
    EEPROM.get(mFlagAddr, mFlag);
}
