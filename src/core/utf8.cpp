#include "core/utf8.h"
#include "core/utf8_encode.h"

namespace termole {

void append_utf8(std::string& output, char32_t codepoint)
{
    if (codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF)) codepoint = 0xFFFD;

    char bytes[4];
    output.append(bytes, termole_utf8_encode(codepoint, bytes));
}

std::string utf8_encode(char32_t codepoint)
{
    std::string result;
    append_utf8(result, codepoint);
    return result;
}

} // namespace termole
