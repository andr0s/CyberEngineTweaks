#pragma once

#ifdef _WIN32
#include "RED4ext/Api/v0/FileVer.hpp"
#include "RED4ext/Api/SemVer.hpp"
#endif

struct Image
{
    void Initialize();

    static std::tuple<uint32_t, uint16_t> GetSupportedVersion() noexcept { return std::make_tuple(2, 31); }

#ifdef _WIN32
    uintptr_t base_address;
    mem::region TextRegion;
    RED4ext::v0::FileVer FileVersion{0, 0, 0, 0};
    RED4ext::v0::SemVer SemVersion{0, 0, 0, 0};
#else
    // The macOS game has no PE version resource. It is identified by its bundle version and by the LC_UUID of the
    // executable; RED4ext's address database is built for exactly one UUID.
    static constexpr const char* SupportedBundleVersion = "2.3.1";
    static constexpr std::array<uint8_t, 16> SupportedUUID{0xA6, 0x65, 0x6A, 0xDC, 0xFB, 0xE2, 0x36, 0xA4,
                                                           0x9B, 0x9D, 0xB4, 0xA9, 0xDE, 0x64, 0x50, 0x89};

    std::string BundleVersion{};
    std::array<uint8_t, 16> UUID{};
    bool IsSupported{false};
#endif
};
