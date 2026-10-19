// Unit tests for src/platform/macos/Utf.h.
// Build: clang++ -std=c++20 -I../../src UtfTests.cpp -o UtfTests

#include <platform/macos/Utf.h>

#include <cstdio>
#include <cstdlib>

static int s_failures = 0;

static void Expect(const bool aCondition, const char* aWhat)
{
    std::printf("%s %s\n", aCondition ? "PASS" : "FAIL", aWhat);
    if (!aCondition)
        ++s_failures;
}

int main()
{
    const std::string samples[] = {"plain ascii", "Grüße aus Night City", "日本語のテキスト", "emoji \xF0\x9F\x98\x80 end", ""};
    for (const auto& sample : samples)
        Expect(Utf::FromWide(Utf::ToWide(sample)) == sample, ("round trip: " + sample).c_str());

    Expect(Utf::ToWide("\xC3\xBC") == std::wstring(1, L'\u00FC'), "2-byte sequence");
    Expect(Utf::ToWide("\xF0\x9F\x98\x80") == std::wstring(1, static_cast<wchar_t>(0x1F600)), "4-byte sequence");
    Expect(Utf::ToWide("\xC0\xAF") == std::wstring(2, static_cast<wchar_t>(0xFFFD)), "overlong '/' rejected");
    Expect(Utf::ToWide("\xED\xA0\x80") == std::wstring(3, static_cast<wchar_t>(0xFFFD)), "encoded surrogate rejected");
    Expect(Utf::ToWide("a\xE2\x82") == std::wstring(L"a") + static_cast<wchar_t>(0xFFFD), "truncated sequence");
    Expect(Utf::ToWide("\xFF") == std::wstring(1, static_cast<wchar_t>(0xFFFD)), "invalid lead byte");
    Expect(Utf::FromWide(std::wstring(1, static_cast<wchar_t>(0x110000))) == "\xEF\xBF\xBD", "out-of-range code point");

    std::printf("%d failure(s)\n", s_failures);
    return s_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
