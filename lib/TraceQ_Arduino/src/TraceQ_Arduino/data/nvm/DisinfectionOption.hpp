#pragma once

#include <stdint.h>
#include "TraceQ_Arduino/data/nvm/NonVolatileData.hpp"
#include "TraceQ_Arduino/data/LocalDateTime.hpp"

class DisinfectionOption : public NonVolatileData
{
public:
    void Upload() override;
    void Load() override;

    int  GetCount() const;
    void SetCount(int count);
    void IncrementCount();

    int  GetMaximumCount() const;
    void SetMaximumCount(int maximumCount);

    uint8_t GetSimultaneousDisinfectionDelay() const;
    void    SetSimultaneousDisinfectionDelay(uint8_t delay);

    uint8_t GetSimultaneousDisinfectionSlot() const;
    void    SetSimultaneousDisinfectionSlot(uint8_t range);

    int  GetClearCount() const;
    void SetClearCount(int clearCount);
    void IncrementClearCount();

    bool          IsClearDateTimeEmpty() const;
    LocalDateTime GetClearDateTime() const;
    void          SetClearDateTime(const LocalDateTime &dateTime);

protected:
    uint8_t mCountAddr{160};
    uint8_t mMaximumCountAddr{162};
    uint8_t mSimultaneousDelayAddr{165};
    uint8_t mSimultaneousSlotAddr{166};
    uint8_t mClearCountAddr{167};
    uint8_t mClearDateTimeAddr{169};

private:
    mutable int           mCount{0};
    mutable int           mMaximumCount{30};
    mutable uint8_t       mSimultaneousDelay{3};
    mutable uint8_t       mSimultaneousSlot{1};
    mutable int           mClearCount{0};
    mutable LocalDateTime mClearDateTime{};
};
