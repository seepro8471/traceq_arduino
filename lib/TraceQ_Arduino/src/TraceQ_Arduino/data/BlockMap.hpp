#pragma once

#include <stdint.h>

// MIFARE Classic 1K: 16 섹터 × 4 블록. 각 섹터의 마지막 블록(3,7,11,...,63)은
// 섹터 트레일러이며 키/AccessBits만 저장. 일반 데이터 쓰기에서 반드시 회피.
constexpr uint8_t MIFARE_BLOCKS_PER_SECTOR{4};
constexpr uint8_t MIFARE_1K_SECTOR_COUNT{16};
constexpr uint8_t MIFARE_4K_SECTOR_COUNT{40};

constexpr inline uint8_t SectorOfBlock(uint8_t block)
{
    // MIFARE 4K의 32번 섹터 이후는 16블록/섹터지만, TraceQ는 1K 영역만 사용.
    return block < 128 ? (block / MIFARE_BLOCKS_PER_SECTOR)
                       : (32 + (block - 128) / 16);
}

constexpr inline uint8_t TrailerOfSector(uint8_t sector)
{
    return sector < 32 ? (sector * MIFARE_BLOCKS_PER_SECTOR + 3)
                       : (128 + (sector - 32) * 16 + 15);
}

constexpr inline bool IsSectorTrailer(uint8_t block)
{
    return block == TrailerOfSector(SectorOfBlock(block));
}

constexpr inline bool IsManufacturerBlock(uint8_t block)
{
    return block == 0; // 섹터0 블록0: 제조사 영역. 쓰기 금지.
}

// --- TraceQ 블록 레이아웃 (1.0 호환) ---
constexpr uint8_t SECTOR0_COMPANY{1};
constexpr uint8_t SECTOR0_TAG{2};

constexpr uint8_t SECTOR1_TAG_SERIAL{4};
constexpr uint8_t SECTOR1_GATEWAY{5};
constexpr uint8_t SECTOR1_PROCESS{6};

constexpr uint8_t SECTOR2_PATIENT_KEY{8};
constexpr uint8_t SECTOR2_PATIENT_NAME{9};
constexpr uint8_t SECTOR2_WASHING_START{10};

constexpr uint8_t SECTOR3_WASHING_START_MANAGER_KEY{12};
constexpr uint8_t SECTOR3_WASHING_START_MANAGER_NAME{13};
constexpr uint8_t SECTOR3_WASHING_END{14};

constexpr uint8_t SECTOR4_WASHING_END_MANAGER_KEY{16};
constexpr uint8_t SECTOR4_WASHING_END_MANAGER_NAME{17};
constexpr uint8_t SECTOR4_DISINFECTION{18}; // deprecated

constexpr uint8_t SECTOR5_DISINFECTION_START{20};
constexpr uint8_t SECTOR5_DISINFECTION_START_MANAGER_KEY{21};
constexpr uint8_t SECTOR5_DISINFECTION_START_MANAGER_NAME{22};

constexpr uint8_t SECTOR6_DISINFECTION_END{24};
constexpr uint8_t SECTOR6_DISINFECTION_END_MANAGER_KEY{25};
constexpr uint8_t SECTOR6_DISINFECTION_END_MANAGER_NAME{26};

constexpr uint8_t SECTOR7_DISINFECTION_START{28};
constexpr uint8_t SECTOR7_DISINFECTION_START_MANAGER_KEY{29};
constexpr uint8_t SECTOR7_DISINFECTION_START_MANAGER_NAME{30};

constexpr uint8_t SECTOR8_DISINFECTION_END{32};
constexpr uint8_t SECTOR8_DISINFECTION_END_MANAGER_KEY{33};
constexpr uint8_t SECTOR8_DISINFECTION_END_MANAGER_NAME{34};

constexpr uint8_t SECTOR14_DISINFECTION_DETAIL{56};
constexpr uint8_t SECTOR14_DISINFECTION_DETAIL2{57};

constexpr uint8_t SECTOR15_EXAMINATION_SUBJECT{60};
constexpr uint8_t SECTOR15_EXAMINATION_SUBJECT2{61};
constexpr uint8_t SECTOR15_EXAMINATION_SUBJECT3{62};
