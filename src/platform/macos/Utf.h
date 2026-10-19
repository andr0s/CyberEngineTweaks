#pragma once

// UTF-8 <-> wide string conversion for macOS, where wchar_t is 32 bits and holds UTF-32 code points. Header-only so it
// can be unit-tested on its own (macos/tests/UtfTests.cpp). Invalid input becomes U+FFFD.

#include <cstdint>
#include <string>
#include <string_view>

namespace Utf
{
inline std::string FromWide(std::wstring_view utf16)
{
    std::string utf8;
    utf8.reserve(utf16.length());
    for (const wchar_t wc : utf16)
    {
        auto cp = static_cast<uint32_t>(wc);
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = 0xFFFD;

        if (cp < 0x80)
            utf8 += static_cast<char>(cp);
        else if (cp < 0x800)
        {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp < 0x10000)
        {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return utf8;
}

inline std::wstring ToWide(std::string_view utf8)
{
    std::wstring wide;
    wide.reserve(utf8.length());
    for (size_t i = 0; i < utf8.length();)
    {
        const auto c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp;
        size_t extra;
        if (c < 0x80)
        {
            cp = c;
            extra = 0;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            cp = c & 0x1F;
            extra = 1;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            cp = c & 0x0F;
            extra = 2;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            cp = c & 0x07;
            extra = 3;
        }
        else
        {
            wide += static_cast<wchar_t>(0xFFFD);
            ++i;
            continue;
        }

        if (i + extra >= utf8.length() && extra != 0)
        {
            wide += static_cast<wchar_t>(0xFFFD);
            break;
        }

        bool valid = true;
        for (size_t k = 1; k <= extra; ++k)
        {
            const auto cc = static_cast<uint8_t>(utf8[i + k]);
            if ((cc & 0xC0) != 0x80)
            {
                valid = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        // reject overlong forms (e.g. C0 AF for '/'), surrogates and values past U+10FFFF
        static constexpr uint32_t kMinForLength[] = {0, 0x80, 0x800, 0x10000};
        if (!valid || cp < kMinForLength[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            wide += static_cast<wchar_t>(0xFFFD);
            ++i;
            continue;
        }

        wide += static_cast<wchar_t>(cp);
        i += extra + 1;
    }
    return wide;
}
} // namespace Utf
