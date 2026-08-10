#pragma once

class NonVolatileData
{
public:
    virtual ~NonVolatileData() = default;
    virtual void Upload() = 0;
    virtual void Load()   = 0;
};
