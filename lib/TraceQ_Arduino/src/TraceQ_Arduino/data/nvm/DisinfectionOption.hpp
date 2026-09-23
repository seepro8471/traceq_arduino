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

    // 두 Set 은 int 로 받아 범위(0~120 / 0~2)를 먼저 자르고 1바이트로 저장한다.
    uint8_t GetSimultaneousDisinfectionDelay() const;
    void    SetSimultaneousDisinfectionDelay(int delay);

    uint8_t GetSimultaneousDisinfectionSlot() const;
    void    SetSimultaneousDisinfectionSlot(int range);

    int  GetClearCount() const;
    void SetClearCount(int clearCount);
    void IncrementClearCount();

    bool          IsClearDateTimeEmpty() const;
    LocalDateTime GetClearDateTime() const;
    void          SetClearDateTime(const LocalDateTime &dateTime);

    // 시계가 방전 표지 시각(2026-01-01)인 동안의 교환일은 미뤄 두었다가 시계가 맞춰질 때 기록한다.
    static constexpr uint8_t kPendingNone{0};
    static constexpr uint8_t kPendingNow{1};       // 클리어 태그 — 맞춰진 시각이 교환일
    static constexpr uint8_t kPendingDefault{2};   // 첫 부팅 기본값 — 맞춰진 시각의 1개월 전
    uint8_t GetClearPending() const;
    bool    HasPendingClear() const { return GetClearPending() != kPendingNone; }
    void    SetClearPending(uint8_t kind);
    void    ApplyPendingClear(const LocalDateTime &now);   // 미뤄 둔 게 없으면 아무것도 안 함

    static LocalDateTime OneMonthBefore(LocalDateTime t);

protected:
    uint8_t mCountAddr{160};
    uint8_t mMaximumCountAddr{162};
    uint8_t mSimultaneousDelayAddr{165};
    uint8_t mSimultaneousSlotAddr{166};
    uint8_t mClearCountAddr{167};
    uint8_t mClearDateTimeAddr{169};   // LocalDateTime 8바이트 → 169~176
    uint8_t mClearPendingAddr{177};

private:
    mutable int           mCount{0};
    mutable int           mMaximumCount{30};
    mutable uint8_t       mSimultaneousDelay{3};
    mutable uint8_t       mSimultaneousSlot{1};
    mutable int           mClearCount{0};
    mutable LocalDateTime mClearDateTime{};
    mutable uint8_t       mClearPending{kPendingNone};
};
