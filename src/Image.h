#pragma once

#ifdef _WIN32
#include "RED4ext/Api/v0/FileVer.hpp"
#include "RED4ext/Api/SemVer.hpp"
#endif

struct Image
{
    void Initialize();

    static std::tuple<uint32_t, uint16_t> GetSupportedVersion() noexcept { return std::make_tuple(2, 50); }

#ifdef _WIN32
    uintptr_t base_address;
    mem::region TextRegion;
    RED4ext::v0::FileVer FileVersion{0, 0, 0, 0};
    RED4ext::v0::SemVer SemVersion{0, 0, 0, 0};
#else
    // The macOS game has no PE version resource. It is identified by its bundle version and by the LC_UUID of the
    // executable; RED4ext's address database is built for exactly one UUID.
    static constexpr const char* SupportedBundleVersion = "2.5.0";
    static constexpr std::array<uint8_t, 16> SupportedUUID{0x7D, 0x73, 0x82, 0xD0, 0xEA, 0xDD, 0x36, 0xF0,
                                                           0xBA, 0x1C, 0xCE, 0xBE, 0xC1, 0xD5, 0xBE, 0xBC};

    std::string BundleVersion{};
    std::array<uint8_t, 16> UUID{};
    bool IsSupported{false};
#endif
};
