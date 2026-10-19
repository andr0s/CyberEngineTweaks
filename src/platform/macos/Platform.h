#pragma once

// macOS helpers for the few places where CET asks the OS about the game process.

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

struct mach_header_64;

namespace Platform
{
// Absolute path of the game executable (<game>/Cyberpunk2077.app/Contents/MacOS/Cyberpunk2077).
std::filesystem::path GetExecutablePath();

// The game's own Mach-O image. Never assumes it is dyld image 0: with DYLD_INSERT_LIBRARIES the order can differ.
const mach_header_64* GetGameImageHeader();
intptr_t GetGameImageSlide();

// Start and end of the game image's __TEXT segment, and of all its mapped segments except __PAGEZERO and
// __LINKEDIT (for diagnostics only).
std::pair<uintptr_t, uintptr_t> GetGameTextRange();
std::pair<uintptr_t, uintptr_t> GetGameImageRange();

// LC_UUID of the game executable.
std::optional<std::array<uint8_t, 16>> GetGameUUID();

// CFBundleShortVersionString / CFBundleName of the game bundle ("2.3.1", "Cyberpunk2077").
std::string GetGameBundleVersion();
std::string GetGameBundleName();

// First preferred UI language, e.g. "ru-RU" (replaces GetSystemDefaultLangID for the font defaults).
std::string GetPreferredLanguage();
} // namespace Platform
