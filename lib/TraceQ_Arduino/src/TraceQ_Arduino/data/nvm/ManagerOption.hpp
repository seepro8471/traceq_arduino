#pragma once

#include <stdint.h>
#include <string.h>
#include "TraceQ_Arduino/data/nvm/NonVolatileData.hpp"

class ManagerOption : public NonVolatileData
{
public:
    static constexpr uint8_t KEY_SIZE{16};
    static constexpr uint8_t NAME_SIZE{16};

    void Upload() override;
    void Load() override;

    // outBuffer / inSize 안전 검사 후 복사. size > 16이면 16으로 클램프.
    void GetKey(unsigned char *outBuffer, uint8_t size) const;
    void SetKey(const unsigned char *buffer);

    void GetName(unsigned char *outBuffer, uint8_t size) const;
    void SetName(const unsigned char *buffer);

    bool HasData() const;

protected:
    uint8_t mKeyAddr{32};
    uint8_t mNameAddr{48};
    uint8_t mFlagAddr{65};

private:
    void setManagerFlag(bool flag);

    mutable unsigned char mKey[KEY_SIZE]{};
    mutable unsigned char mName[NAME_SIZE]{};
    mutable bool          mFlag{false};
};
