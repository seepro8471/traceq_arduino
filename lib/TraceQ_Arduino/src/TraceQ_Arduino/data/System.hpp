#pragma once

#include <stdint.h>

// 기기 타입 (DeviceOption에 char로 저장).
constexpr char GATEWAY_TYPE_DEVICE{'G'};
constexpr char WASHING_TYPE_DEVICE{'W'};
constexpr char DISINFECTION_TYPE_DEVICE{'D'};
constexpr char SERVER_TYPE_DEVICE{'S'};

// 태그 종류 (Company.TagType에 uint8로 저장).
constexpr uint8_t SCOPE_TYPE_TAG{5};
constexpr uint8_t MANAGER_TYPE_TAG{6};
constexpr uint8_t CLEAR_TYPE_TAG{7};

// 시리얼 프레임 STX/ETX.
constexpr char STX{'\x02'};
constexpr char ETX{'\x03'};
