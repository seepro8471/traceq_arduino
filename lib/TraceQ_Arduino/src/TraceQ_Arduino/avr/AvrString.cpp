#include "TraceQ_Arduino/avr/AvrString.hpp"

namespace
{
inline bool is_digit_char(char c) { return c >= '0' && c <= '9'; }
}

size_t str_strlen(const char *s)
{
    if (s == nullptr) return 0;
    size_t i = 0;
    while (i < TRACEQ_STR_MAX_SCAN_LEN && s[i]) ++i;
    return i;
}

size_t str_strlen_unsigned(const unsigned char *s)
{
    return str_strlen(reinterpret_cast<const char *>(s));
}

bool str_all_match(const unsigned char *src, const char condition)
{
    if (src == nullptr) return false;
    const auto len = str_strlen_unsigned(src);
    if (len == 0) return false;
    for (size_t i = 0; i < len; ++i)
        if (src[i] != static_cast<unsigned char>(condition)) return false;
    return true;
}

bool str_all_match_cstr(const unsigned char *src, const unsigned char *condition)
{
    if (src == nullptr || condition == nullptr) return false;
    const auto a = str_strlen_unsigned(src);
    const auto b = str_strlen_unsigned(condition);
    if (a == 0 || b == 0 || a != b) return false;
    return memcmp(src, condition, a) == 0;
}

int str_atoi(const char *s)
{
    if (s == nullptr || *s == '\0') return -1;
    int res = 0;
    for (size_t i = 0; i < TRACEQ_STR_MAX_SCAN_LEN && s[i] != '\0'; ++i)
    {
        if (!is_digit_char(s[i])) return -1; // 비숫자 거부 (1.0은 무조건 진행).
        res = res * 10 + (s[i] - '0');
        if (res < 0) return -1; // overflow.
    }
    return res;
}

int str_atoi_range(const char *s, uint8_t begin, uint8_t end)
{
    if (s == nullptr) return -1;
    const auto len = str_strlen(s);
    if (begin >= len) return -1;
    if (end >= len) end = static_cast<uint8_t>(len - 1);
    if (begin > end) return -1;
    int res = 0;
    for (uint8_t i = begin; i <= end; ++i)
    {
        if (!is_digit_char(s[i])) return -1;
        res = res * 10 + (s[i] - '0');
        if (res < 0) return -1;
    }
    return res;
}

int str_atoi_unsigned(const unsigned char *s)
{
    return str_atoi(reinterpret_cast<const char *>(s));
}

bool str_contains(const char *src, char c)
{
    if (src == nullptr) return false;
    const auto len = str_strlen(src);
    for (size_t i = 0; i < len; ++i) if (src[i] == c) return true;
    return false;
}

char *str_find_char(const char *str, char c)
{
    if (str == nullptr) return nullptr;
    for (size_t i = 0; i < TRACEQ_STR_MAX_SCAN_LEN && str[i] != '\0'; ++i)
        if (str[i] == c) return const_cast<char *>(str + i);
    return nullptr;
}

size_t str_index_of(const char *src, char c)
{
    return str_index_of_range(src, c, 0);
}

size_t str_index_of_range(const char *src, char c, size_t begin)
{
    if (src == nullptr) return static_cast<size_t>(-1);
    const auto len = str_strlen(src);
    if (begin >= len) return static_cast<size_t>(-1);
    const char *p = strchr(src + begin, c);
    return p ? static_cast<size_t>(p - src) : static_cast<size_t>(-1);
}

size_t str_index_of_cstr(const char *src, const char *c)
{
    return str_index_of_cstr_range(src, c, 0);
}

size_t str_index_of_cstr_range(const char *src, const char *c, size_t begin)
{
    if (src == nullptr || c == nullptr) return static_cast<size_t>(-1);
    const auto len = str_strlen(src);
    if (len == 0 || begin >= len) return static_cast<size_t>(-1);
    const char *p = strstr(src + begin, c);
    return p ? static_cast<size_t>(p - src) : static_cast<size_t>(-1);
}

void str_substring_safe(const char *src, char *dst, size_t dstSize,
                        size_t begin, size_t end)
{
    if (dst == nullptr || dstSize == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;

    if (begin > end) { size_t t = begin; begin = end; end = t; }
    const size_t srcLen = str_strlen(src);
    if (begin >= srcLen) return;
    if (end > srcLen) end = srcLen;

    size_t copyLen = end - begin;
    if (copyLen >= dstSize) copyLen = dstSize - 1;  // 항상 NUL 자리 보존.
    memcpy(dst, src + begin, copyLen);
    dst[copyLen] = '\0';
}

char str_to_char(uint8_t num)
{
    return (num <= 9) ? static_cast<char>(num + '0') : '0';
}
