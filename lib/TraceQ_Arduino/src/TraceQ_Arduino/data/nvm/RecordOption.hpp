#pragma once

#include <stdint.h>
#include "TraceQ_Arduino/data/nvm/NonVolatileData.hpp"

class RecordOption : public NonVolatileData
{
public:
    void Upload() override;
    void Load() override;

    bool GetManagerDisposability() const;
    void SetManagerDisposability(bool flag);
    bool GetPatientCheck() const;
    void SetPatientCheck(bool flag);

protected:
    uint8_t mManagerDisposabilityAddr{128};
    uint8_t mPatientCheckAddr{129};

private:
    mutable bool mManagerDisposability{false};
    mutable bool mPatientCheck{true};
};
