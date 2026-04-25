#pragma once
#include <string>
#include <cstring>
#include <algorithm>

std::string TrimNulls(const std::string& input);
std::string Trim(const std::string& str);

// Safe string copy that replaces strncpy to avoid MSVC C4996 warning.
// Copies up to `max_chars - 1` characters from src to dest, then null-terminates.
inline void fai_strncpy(char* dest, const char* src, size_t max_chars) {
    if (!dest || !src || max_chars == 0) return;
    const size_t len = std::min(strlen(src), max_chars - 1);
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// Overload for std::string source
inline void fai_strncpy(char* dest, const std::string& src, size_t max_chars) {
    fai_strncpy(dest, src.c_str(), max_chars);
}
