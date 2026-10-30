#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace termole {

// Incremental UTF-8 decoder. Malformed sequences produce U+FFFD without
// throwing and without desynchronizing the remaining input.
class Utf8Decoder {
public:
    template <class Emit>
    void feed(const char* data, size_t size, Emit&& emit)
    {
        size_t index = 0;
        while (index < size) {
            const auto byte = static_cast<uint8_t>(data[index]);
            if (remaining_ == 0) {
                if (byte < 0x80) {
                    emit(static_cast<char32_t>(byte));
                    ++index;
                } else if ((byte & 0xE0) == 0xC0) {
                    codepoint_ = byte & 0x1F;
                    minimum_ = 0x80;
                    remaining_ = 1;
                    ++index;
                } else if ((byte & 0xF0) == 0xE0) {
                    codepoint_ = byte & 0x0F;
                    minimum_ = 0x800;
                    remaining_ = 2;
                    ++index;
                } else if ((byte & 0xF8) == 0xF0) {
                    codepoint_ = byte & 0x07;
                    minimum_ = 0x10000;
                    remaining_ = 3;
                    ++index;
                } else {
                    emit(static_cast<char32_t>(0xFFFD));
                    ++index;
                }
                continue;
            }

            if ((byte & 0xC0) == 0x80) {
                codepoint_ = (codepoint_ << 6) | (byte & 0x3F);
                --remaining_;
                ++index;
                if (remaining_ == 0) {
                    const bool invalid = codepoint_ < minimum_ || codepoint_ > 0x10FFFF ||
                        (codepoint_ >= 0xD800 && codepoint_ <= 0xDFFF);
                    emit(invalid ? static_cast<char32_t>(0xFFFD) : codepoint_);
                    codepoint_ = 0;
                    minimum_ = 0;
                }
                continue;
            }

            // A non-continuation byte aborts the current sequence; reprocess it
            // as a new lead byte.
            emit(static_cast<char32_t>(0xFFFD));
            codepoint_ = 0;
            minimum_ = 0;
            remaining_ = 0;
        }
    }

private:
    uint32_t codepoint_ = 0;
    uint32_t minimum_ = 0;
    int remaining_ = 0;
};

void append_utf8(std::string& output, char32_t codepoint);
std::string utf8_encode(char32_t codepoint);

} // namespace termole
