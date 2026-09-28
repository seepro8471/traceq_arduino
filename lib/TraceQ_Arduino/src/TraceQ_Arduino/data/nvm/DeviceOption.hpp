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
    /// 기기번호 상한 — 화면 5칸(" W:99"/"W:999"). 세터·게터·게이트웨이 G1 관문(RAM·EEPROM 둘 다)이 **이 하나**를 쓴다.
    ///  관문이 없으면 세터가 1000 이상을 999 로 잘라 **틀린 번호 999 가 저장된다**(14차 II-E 측정 — 13차에 내가
    ///  "전문마다 EEPROM 을 쓴다" 고 적은 것은 틀렸다: put 은 같은 바이트를 안 쓴다).
    static constexpr int kNumberMax = 999;
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
