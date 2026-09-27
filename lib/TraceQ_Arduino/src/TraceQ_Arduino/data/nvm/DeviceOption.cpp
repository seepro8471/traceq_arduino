#include <EEPROM.h>
#include "TraceQ_Arduino/data/nvm/DeviceOption.hpp"
#include "TraceQ_Arduino/data/System.hpp"

void DeviceOption::Upload()
{
    // ★타입을 마지막에 — HasStoredSettings() 가 타입만 보므로, 쓰는 중 전원이 끊기면 '설정 없음' 이 되어
    //  다음 부팅이 묻지 않고 스스로 재소거한다. 반대 순서는 번호 없는 채 굳었다(Z1 P3-3).
    EEPROM.put(mNumberAddr, mNumber);
    EEPROM.put(mTypeAddr, mType);
}

void DeviceOption::Load()
{
    EEPROM.get(mTypeAddr, mType);
    EEPROM.get(mNumberAddr, mNumber);
}

bool DeviceOption::HasStoredSettings() const
{
    // 폴백 없는 원값 — 공장 초기(0xFF)·손상과 "설정이 들어 있는 기기" 를 가른다.
    char type{};
    EEPROM.get(mTypeAddr, type);
    return type == GATEWAY_TYPE_DEVICE || type == WASHING_TYPE_DEVICE ||
           type == DISINFECTION_TYPE_DEVICE || type == SERVER_TYPE_DEVICE;
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
    return mNumber < 0 ? 0 : mNumber > 999 ? 999 : mNumber;   // 세터 범위 밖 값은 돌려주지 않는다
}

// [10차 판정 · 재론 금지] 2바이트 int 를 쓰는 중 전원이 끊기면 **사람이 고르지 않은 유효값**이 남는다
//  (실측 300 → 44 = 낮은 바이트만 써진 것 · 창 3ms). 범위 검사로는 못 가른다(44 도 유효값). 표지·검사합을
//  두면 설정마다 상태가 늘고, 방아쇠는 전원 불안정뿐(사장님 판정 09-27)이라 그대로 둔다.
//  다시 볼 조건: 전원이 불안정한 현장에서 기기번호·MaxCount 가 저 혼자 바뀐 로그가 나오면.
void DeviceOption::SetNumber(int number)
{
    if (number < 0) number = 0;
    if (number > 999) number = 999;   // 화면 5칸(" W:99"/"W:999") 상한 — PC 설정기는 0~99 만 보낸다
    EEPROM.put(mNumberAddr, number);
    EEPROM.get(mNumberAddr, mNumber);
}
