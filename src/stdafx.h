#pragma once

#ifdef __APPLE__
#include "platform/macos/WinCompat.h"
#include "platform/macos/Platform.h"
#endif

#include "reverse/Relocation.h"

#include <sol/sol.hpp>
#include <spdlog/spdlog.h>
#include <imgui.h>
#ifdef _WIN32
#include <MinHook.h>
#include <mem/module.h>
#include <mem/pattern.h>
#endif
#include <nlohmann/json.hpp>

#include <RED4ext/CName.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/CString.hpp>
#include <RED4ext/DynArray.hpp>
#include "RED4ext/GameApplication.hpp"
#include <RED4ext/GameEngine.hpp>
#include <RED4ext/Hashing/CRC.hpp>
#include <RED4ext/ISerializable.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/ResourceDepot.hpp>
#include <RED4ext/RTTISystem.hpp>
#include <RED4ext/RTTITypes.hpp>
#include <RED4ext/Scripting/CProperty.hpp>
#include <RED4ext/Scripting/Functions.hpp>
#include <RED4ext/Scripting/OpcodeHandlers.hpp>
#include <RED4ext/Scripting/Stack.hpp>
#include <RED4ext/Scripting/Natives/ScriptGameInstance.hpp>
#include <RED4ext/TweakDB.hpp>
#ifdef __APPLE__
#include "platform/macos/SdkCompat.h"
#endif

#include <TiltedCore/Allocator.hpp>
#include <TiltedCore/Lockable.hpp>
#include <TiltedCore/Platform.hpp>
#include <TiltedCore/ScratchAllocator.hpp>
#include <TiltedCore/Signal.hpp>
#include <TiltedCore/StackAllocator.hpp>
#include <TiltedCore/TaskQueue.hpp>

#ifdef _WIN32
#include <tchar.h>
#include <d3d12.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DbgHelp.h>
#include <dxgi1_4.h>
#include <SDKDDKVer.h>
#include <Windows.h>
#include <wrl.h>
#else
#include "platform/macos/Hooking.h"
#endif

#include <algorithm>
#include <array>
#include <bitset>
#include <compare>
#include <cstdint>
#include <execution>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <shared_mutex>
#include <source_location>
#include <unordered_map>
#include <vector>

#include "common/Logging.h"
#include "common/RedCompat.h"
#include "common/FontMaterialDesignIcons.h"
#include "common/ImGuiNotify.h"
#include "Options.h"
#include "Paths.h"
#include "PersistentState.h"
#include "reverse/Addresses.h"
#include "scripting/GameHooks.h"
#include "VKBindings.h"

template <> struct std::hash<RED4ext::CName>
{
    std::size_t operator()(RED4ext::CName aKey) const noexcept { return static_cast<size_t>(aKey.hash); }
};
