#pragma once

struct PatientKey
{
    unsigned char Key[16]{};
    PatientKey() = default;
};

struct PatientName
{
    unsigned char Name[16]{};
    PatientName() = default;
};
