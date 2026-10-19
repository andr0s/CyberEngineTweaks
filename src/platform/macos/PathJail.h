#pragma once

// Path confinement for mod file access on macOS (POSIX paths, APFS). Header-only so it can be unit-tested on its own
// (macos/tests/PathJailTests.cpp).

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>

namespace PathJail
{
inline std::string FoldCase(std::string aString)
{
    std::ranges::transform(aString, aString.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return aString;
}

// Mods are written for Windows and may use '\' as a separator; on macOS it would be part of a file name.
inline std::filesystem::path NormalizeSeparators(const std::filesystem::path& acPath)
{
    auto str = acPath.string();
    std::ranges::replace(str, '\\', '/');
    return {str};
}

// True if acPath is acRoot or lies inside it. Both must be canonical. APFS is case-insensitive by default, so the
// components are compared without case.
inline bool IsInside(const std::filesystem::path& acPath, const std::filesystem::path& acRoot)
{
    auto pathIt = acPath.begin();
    for (auto rootIt = acRoot.begin(); rootIt != acRoot.end(); ++rootIt)
    {
        if (rootIt->empty())
            continue; // trailing separator
        if (pathIt == acPath.end() || FoldCase(pathIt->string()) != FoldCase(rootIt->string()))
            return false;
        ++pathIt;
    }
    return true;
}

// Resolves a path a mod asked for, relative to its root folder. Both are resolved with weakly_canonical, which follows
// every symlink in the existing part of the path and normalizes "." and "..", and the result must lie inside the root.
// Like the Windows build, a symlink as the requested file itself is refused. Returns an absolute path, or an empty
// path when the request is refused.
inline std::filesystem::path Resolve(std::filesystem::path aFilePath, const std::filesystem::path& acRootPath, const bool acAllowNonExisting)
{
    if (aFilePath.empty())
        return {};

    aFilePath = NormalizeSeparators(aFilePath);
    if (aFilePath.has_root_path())
        return {};

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(acRootPath, ec);
    if (ec || root.empty())
        return {};

    const auto candidate = root / aFilePath;

    if (std::filesystem::is_symlink(candidate, ec))
        return {};

    const auto resolved = std::filesystem::weakly_canonical(candidate, ec);
    if (ec || resolved.empty() || !IsInside(resolved, root))
        return {};

    if (!acAllowNonExisting && !std::filesystem::exists(resolved, ec))
        return {};

    return resolved;
}

// True if two resolved paths name the same file (case-insensitive, as APFS by default).
inline bool IsSamePath(const std::filesystem::path& acLhs, const std::filesystem::path& acRhs)
{
    return !acLhs.empty() && FoldCase(acLhs.string()) == FoldCase(acRhs.string());
}
} // namespace PathJail
