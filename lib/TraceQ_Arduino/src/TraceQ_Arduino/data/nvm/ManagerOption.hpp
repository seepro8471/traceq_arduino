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
    void GetName(unsigned char *outBuffer, uint8_t size) const;

    /// 키·이름을 함께 저장한다(둘 중 하나라도 nullptr 이면 칸을 비우고 담당자 없음).
    /// 표지는 먼저 내리고 바이트를 다 넣은 뒤 한 번만 올린다 — 쓰는 중 전원이 끊기면 표지가 거짓이라
    /// 반쪽 담당자가 '등록됨' 이 되지 않는다(Y2 P3-2). ★표지가 거짓일 때 칸에는 반쪽이 남아 있을 수 있으니
    /// 읽는 쪽이 HasData() 를 먼저 본다(Z1 P3-2). 바뀌는 것이 없으면 한 바이트도 쓰지 않는다(Z1 P3-4).
    void SetData(const unsigned char *key, const unsigned char *name);

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
