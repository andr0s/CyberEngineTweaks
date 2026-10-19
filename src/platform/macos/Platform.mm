#include "Platform.h"

#import <Foundation/Foundation.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

namespace
{
uint32_t FindGameImageIndex()
{
    static uint32_t s_index = UINT32_MAX;
    if (s_index != UINT32_MAX)
        return s_index;

    const auto exe = Platform::GetExecutablePath();
    const auto count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const char* name = _dyld_get_image_name(i);
        if (!name)
            continue;

        char resolved[PATH_MAX];
        const auto path = realpath(name, resolved) ? std::filesystem::path(resolved) : std::filesystem::path(name);
        if (path == exe)
        {
            s_index = i;
            return s_index;
        }
    }

    // Fall back to the image that contains the main executable's entry point lookup: index 0.
    s_index = 0;
    return s_index;
}
} // namespace

std::filesystem::path Platform::GetExecutablePath()
{
    static const std::filesystem::path s_path = []
    {
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            return std::filesystem::path{};
        buffer.resize(std::strlen(buffer.c_str()));

        char resolved[PATH_MAX];
        if (realpath(buffer.c_str(), resolved))
            return std::filesystem::path(resolved);
        return std::filesystem::path(buffer);
    }();
    return s_path;
}

const mach_header_64* Platform::GetGameImageHeader()
{
    return reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(FindGameImageIndex()));
}

intptr_t Platform::GetGameImageSlide()
{
    return _dyld_get_image_vmaddr_slide(FindGameImageIndex());
}

std::pair<uintptr_t, uintptr_t> Platform::GetGameTextRange()
{
    const auto* header = GetGameImageHeader();
    if (!header || header->magic != MH_MAGIC_64)
        return {0, 0};

    const auto slide = GetGameImageSlide();
    const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cmd->cmd == LC_SEGMENT_64)
        {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            if (std::strncmp(seg->segname, "__TEXT", sizeof(seg->segname)) == 0)
                return {static_cast<uintptr_t>(seg->vmaddr + slide), static_cast<uintptr_t>(seg->vmaddr + slide + seg->vmsize)};
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    return {0, 0};
}

std::pair<uintptr_t, uintptr_t> Platform::GetGameImageRange()
{
    const auto* header = GetGameImageHeader();
    if (!header || header->magic != MH_MAGIC_64)
        return {0, 0};

    const auto slide = GetGameImageSlide();
    uintptr_t begin = UINTPTR_MAX;
    uintptr_t end = 0;
    const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cmd->cmd == LC_SEGMENT_64)
        {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            if (std::strncmp(seg->segname, "__PAGEZERO", sizeof(seg->segname)) != 0 && std::strncmp(seg->segname, "__LINKEDIT", sizeof(seg->segname)) != 0)
            {
                begin = std::min(begin, static_cast<uintptr_t>(seg->vmaddr + slide));
                end = std::max(end, static_cast<uintptr_t>(seg->vmaddr + slide + seg->vmsize));
            }
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    return begin < end ? std::make_pair(begin, end) : std::make_pair(uintptr_t{0}, uintptr_t{0});
}

std::optional<std::array<uint8_t, 16>> Platform::GetGameUUID()
{
    const auto* header = GetGameImageHeader();
    if (!header || header->magic != MH_MAGIC_64)
        return std::nullopt;

    const auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cmd->cmd == LC_UUID)
        {
            const auto* uuid = reinterpret_cast<const uuid_command*>(cmd);
            std::array<uint8_t, 16> result{};
            std::memcpy(result.data(), uuid->uuid, result.size());
            return result;
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    return std::nullopt;
}

std::string Platform::GetGameBundleVersion()
{
    @autoreleasepool
    {
        NSString* version = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
        return version ? std::string([version UTF8String]) : std::string{};
    }
}

std::string Platform::GetGameBundleName()
{
    @autoreleasepool
    {
        NSString* name = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleName"];
        return name ? std::string([name UTF8String]) : std::string{};
    }
}

std::string Platform::GetPreferredLanguage()
{
    @autoreleasepool
    {
        NSArray<NSString*>* languages = [NSLocale preferredLanguages];
        if (languages.count == 0)
            return {};
        return std::string([languages[0] UTF8String]);
    }
}
