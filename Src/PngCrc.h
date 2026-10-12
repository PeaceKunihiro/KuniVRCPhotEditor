#pragma once
#include <array>
#include <cstdint>
#include <string_view>

inline uint32_t PngCrcUpdate(uint32_t crc, std::string_view bytes) {
    static const auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t b = 0; b < 256; ++b) {
            auto c = b;
            for (int i = 0; i < 8; ++i) c = (c >> 1) ^ ((c & 1) ? 0xedb88320U : 0);
            values[b] = c;
        }
        return values;
    }();
    for (unsigned char b : bytes) crc = (crc >> 8) ^ table[(crc ^ b) & 255];
    return crc;
}
inline uint32_t PngCrc(std::string_view bytes) { return ~PngCrcUpdate(~0U, bytes); }
