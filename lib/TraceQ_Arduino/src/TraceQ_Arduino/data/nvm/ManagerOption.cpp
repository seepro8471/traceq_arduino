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

void ManagerOption::SetData(const unsigned char *key, const unsigned char *name)
{
    unsigned char newKey[KEY_SIZE]{}, newName[NAME_SIZE]{};
    if (key != nullptr)  memcpy(newKey, key, KEY_SIZE);
    if (name != nullptr) memcpy(newName, name, NAME_SIZE);
    const bool want = (key != nullptr && name != nullptr);
    // 바뀌는 것이 없으면 한 바이트도 쓰지 않는다 — 같은 담당자를 다시 댈 때마다 표지가 1→0→1 로 2회 닳았다(Z1 P3-4).
    EEPROM.get(mKeyAddr, mKey);
    EEPROM.get(mNameAddr, mName);
    if (memcmp(mKey, newKey, KEY_SIZE) == 0 && memcmp(mName, newName, NAME_SIZE) == 0 && HasData() == want) return;

    setManagerFlag(false);              // 먼저 내린다 — 이 아래에서 끊기면 '담당자 없음'(안전한 쪽)
    memcpy(mKey, newKey, KEY_SIZE);
    EEPROM.put(mKeyAddr, mKey);
    memcpy(mName, newName, NAME_SIZE);
    EEPROM.put(mNameAddr, mName);
    if (want) setManagerFlag(true);
}

void ManagerOption::GetName(unsigned char *outBuffer, uint8_t size) const
{
    if (outBuffer == nullptr || size == 0) return;
    if (size > NAME_SIZE) size = NAME_SIZE;
    memset(outBuffer, 0, size);
    EEPROM.get(mNameAddr, mName);
    memcpy(outBuffer, mName, size);
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
