#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/**
 * TraceQ Arduino 2.0 — 안전한 문자열 유틸.
 *
 * 1.0의 AvrString_*.cpp 대비 변경점:
 *   - 모든 substring 변환에 명시적 dst 크기를 요구한다 (`str_substring_safe`).
 *   - 1.0의 `str_substring_range`는 `dst[end] = '\0'`로 인덱스를 잘못 사용해
 *     OOB write가 발생할 수 있었음 — 2.0은 항상 길이로 인덱싱.
 *   - `str_strlen` 계열은 nullptr / 최대 스캔 길이(MAX_SCAN_LEN)를 항상 검사.
 *   - 정수 변환은 unsigned 음수 입력 / 비숫자 문자 거부.
 */

constexpr size_t TRACEQ_STR_MAX_SCAN_LEN{512};

// 데이터 도메인 상한 — Tag/Manager 등에서 사용.
constexpr size_t TRACEQ_TAG_ID_MAX{14};        // Tag ID
constexpr size_t TRACEQ_TAG_SERIAL_MAX{16};    // TagSerial.Serial
constexpr size_t TRACEQ_MANAGER_KEY_MAX{14};
constexpr size_t TRACEQ_MANAGER_NAME_MAX{16};

bool   str_all_match(const unsigned char *src, char condition);
bool   str_all_match_cstr(const unsigned char *src, const unsigned char *condition);

int    str_atoi(const char *string);
int    str_atoi_range(const char *string, uint8_t begin, uint8_t end);
int    str_atoi_unsigned(const unsigned char *string);

bool   str_contains(const char *src, char c);

char  *str_find_char(const char *str, char c);

size_t str_index_of(const char *src, char c);
size_t str_index_of_range(const char *src, char c, size_t begin);
size_t str_index_of_cstr(const char *src, const char *c);
size_t str_index_of_cstr_range(const char *src, const char *c, size_t begin);

size_t str_strlen(const char *string);
size_t str_strlen_unsigned(const unsigned char *string);

/// 안전한 substring (항상 dst 크기 인자 요구). 결과는 항상 NUL-종료.
void   str_substring_safe(const char *src, char *dst, size_t dstSize,
                          size_t begin, size_t end);

/// 0..9 → '0'..'9'. 범위 밖이면 '0'.
char   str_to_char(uint8_t num);
