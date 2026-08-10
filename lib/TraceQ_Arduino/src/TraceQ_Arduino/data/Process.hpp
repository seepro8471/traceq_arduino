#pragma once

#include <stdint.h>

struct Process
{
    uint8_t Status{0};
    uint8_t DisinfectionCount{0};
    uint8_t WashingStatus{0};
    uint8_t DisinfectionStatus{0};
    int     MachineNumber{0};
    bool    MovementNeeded{false};
    uint8_t LegacyRewrite{0}; // deprecated, 레이아웃 호환 유지.
    uint8_t Rewrite{0};

    Process() = default;
    Process(uint8_t s, uint8_t dc, uint8_t ws, uint8_t ds, int mn,
            bool mv, uint8_t lrw, uint8_t rw)
        : Status(s), DisinfectionCount(dc), WashingStatus(ws),
          DisinfectionStatus(ds), MachineNumber(mn), MovementNeeded(mv),
          LegacyRewrite(lrw), Rewrite(rw) {}
    explicit Process(uint8_t s) : Process(s, 0, 0, 0, 0, false, 0, 0) {}
};
