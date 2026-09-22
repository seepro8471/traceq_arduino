#pragma once

#include <stdint.h>
#include "TraceQ_Arduino/data/nvm/NonVolatileData.hpp"

class AlarmOption : public NonVolatileData
{
public:
    void Upload() override;
    void Load() override;

    bool   GetFlag() const;
    void   SetFlag(bool flag);
    // Set 은 int 로 받아 0~127 로 자른 뒤 1바이트로 저장한다(좁힌 뒤 자르면 200→음수, 300→44).
    int8_t GetTimeSlot1() const;
    void   SetTimeSlot1(int minute);
    int8_t GetTimeSlot2() const;
    void   SetTimeSlot2(int minute);

protected:
    // 1.0과 동일한 EEPROM 주소 (데이터 호환).
    uint8_t mSoundAddr{96};
    uint8_t mTimeSlot1Addr{97};
    uint8_t mTimeSlot2Addr{98};

private:
    // const getter에서 EEPROM 재로드를 허용하기 위해 mutable.
    mutable bool   mSound{true};
    mutable int8_t mTimeSlot1{4};
    mutable int8_t mTimeSlot2{18};
};
