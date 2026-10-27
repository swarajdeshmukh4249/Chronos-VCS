#include "core/ContentHash.h"

#include <cstdint>

namespace {

const std::uint64_t kOffsetBasis = 14695981039346656037ULL;
const std::uint64_t kPrime = 1099511628211ULL;
const char* const kHexDigits = "0123456789abcdef";

}  // namespace

std::string contentHash(const std::string& content) {
    std::uint64_t hash = kOffsetBasis;
    for (char c : content) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kPrime;
    }
    std::string hex(16, '0');
    for (int i = 15; i >= 0; --i) {
        hex[static_cast<std::size_t>(i)] = kHexDigits[hash & 0xF];
        hash >>= 4;
    }
    return hex;
}

bool isContentHash(const std::string& text) {
    if (text.size() != 16) {
        return false;
    }
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}
