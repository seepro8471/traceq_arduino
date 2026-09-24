#pragma once

#include <stdint.h>
#include "TraceQ_Arduino/data/nvm/NonVolatileData.hpp"

class DeviceOption : public NonVolatileData
{
public:
    void Upload() override;
    void Load() override;

    char GetType() const;
    void SetType(char type);
    /// 쓰던 기기인가(타입 원값이 W/D/S/G) — 공장 초기·손상이면 false.
    bool HasStoredSettings() const;
    int  GetNumber() const;
    void SetNumber(int number);

protected:
    // 주소 0 은 과거 LatestCompat(서버 Latest/Old 갈래) 플래그 자리 —
    // 2.2.0 에서 Latest 갈래 삭제(사용자 확정)로 미사용. 기존 기기 EEPROM
    // 호환을 위해 주소는 비워 두고 재사용하지 않는다.
    uint8_t  mTypeAddr{1};
    uint8_t  mNumberAddr{2};

private:
    mutable char mType{'W'};
    mutable int  mNumber{1};
};
