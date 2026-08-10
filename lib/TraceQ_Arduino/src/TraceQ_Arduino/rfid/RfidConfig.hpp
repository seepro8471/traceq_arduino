#pragma once

#include <stdint.h>

// 컴파일 시 platformio.ini build_flags에서 오버라이드 가능.
#ifndef TRACEQ_RFID_VERIFY_WRITES
#define TRACEQ_RFID_VERIFY_WRITES 1
#endif

#ifndef TRACEQ_RFID_MAX_WRITE_RETRIES
#define TRACEQ_RFID_MAX_WRITE_RETRIES 3
#endif

#ifndef TRACEQ_RFID_SOFTRESET_TIMEOUT_MS
#define TRACEQ_RFID_SOFTRESET_TIMEOUT_MS 150
#endif

// MIFARE Classic 블록 크기는 16 byte 고정.
constexpr uint8_t MIFARE_BLOCK_SIZE{16};

// MIFARE Read는 16+CRC(2) = 18 byte를 반환.
constexpr uint8_t MIFARE_READ_BUFFER_SIZE{18};

enum class RfidResult : uint8_t
{
    Ok = 0,
    NoTag,
    AuthFailed,
    WriteFailed,
    ReadFailed,
    VerifyMismatch,
    ProtectedBlock,    // 트레일러/제조사 블록 보호
    InvalidArgument,
    NotInitialized,
    UnsupportedPicc,
};

inline const __FlashStringHelper *RfidResultName(RfidResult r)
{
    switch (r)
    {
    case RfidResult::Ok:              return F("Ok");
    case RfidResult::NoTag:           return F("NoTag");
    case RfidResult::AuthFailed:      return F("AuthFailed");
    case RfidResult::WriteFailed:     return F("WriteFailed");
    case RfidResult::ReadFailed:      return F("ReadFailed");
    case RfidResult::VerifyMismatch:  return F("VerifyMismatch");
    case RfidResult::ProtectedBlock:  return F("ProtectedBlock");
    case RfidResult::InvalidArgument: return F("InvalidArgument");
    case RfidResult::NotInitialized:  return F("NotInitialized");
    case RfidResult::UnsupportedPicc: return F("UnsupportedPicc");
    }
    return F("Unknown");
}
