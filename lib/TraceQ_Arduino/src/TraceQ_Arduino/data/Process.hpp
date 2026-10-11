#pragma once

#include <stddef.h>
#include <stdint.h>

struct Process
{   // 태그 블록 6 의 9바이트와 같은 레이아웃 — 아래 static_assert 가 크기와 **칸마다의 위치**를 지킨다(18차 · 순서를 바꾸면 빌드 실패)
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
static_assert(sizeof(Process) == 9, "Process must be exactly the 9 tag bytes (BaseProcessor reads/writes 9)");
static_assert(offsetof(Process, Status) == 0 && offsetof(Process, DisinfectionCount) == 1 && offsetof(Process, WashingStatus) == 2 &&
              offsetof(Process, DisinfectionStatus) == 3 && offsetof(Process, MachineNumber) == 4 && offsetof(Process, MovementNeeded) == 6 &&
              offsetof(Process, LegacyRewrite) == 7 && offsetof(Process, Rewrite) == 8, "Process layout must match the tag block byte for byte");

