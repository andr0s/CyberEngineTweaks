// macOS entry point: Cyber Engine Tweaks runs as a RED4ext plugin (red4ext/plugins/CyberEngineTweaks/), replacing
// dllmain.cpp and the Windows ASI loader. Hooks go through RED4ext (platform/macos/Hooking.h) and the game's main
// thread is reached through RED4ext's game-state callbacks instead of hooks on the state tick functions.

#include <stdafx.h>

#include "CET.h"
#include "config/CETVersion.h"
#include "platform/macos/Hooking.h"
#include "platform/macos/InputMac.h"

#include <RED4ext/Api/ApiVersion.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/GameState.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Runtime.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/Version.hpp>
#include <RED4ext/GameStates.hpp>

namespace
{
bool s_initialized = false;

bool DrainState(const RED4ext::EGameStateType aState)
{
    return !s_initialized || GameMainThread::Get().DrainState(aState);
}

bool OnBaseInitializationEnter(RED4ext::CGameApplication*)
{
    if (s_initialized)
        CET::Get().GetVM().OnBaseInitialization();
    return true;
}

bool OnBaseInitializationUpdate(RED4ext::CGameApplication*)
{
    // Returning false would hold the game in this state, so only report pending tasks.
    DrainState(RED4ext::EGameStateType::BaseInitialization);
    return true;
}

bool OnInitializationUpdate(RED4ext::CGameApplication*)
{
    DrainState(RED4ext::EGameStateType::Initialization);
    return true;
}

bool OnRunningEnter(RED4ext::CGameApplication*)
{
    if (s_initialized)
        CET::Get().GetVM().OnRunningEnter();
    return true;
}

bool OnRunningUpdate(RED4ext::CGameApplication*)
{
    DrainState(RED4ext::EGameStateType::Running);
    return false; // keep being called every tick
}

bool OnShutdownUpdate(RED4ext::CGameApplication*)
{
    DrainState(RED4ext::EGameStateType::Shutdown);
    return true;
}

void RegisterStates(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk)
{
    static RED4ext::v1::GameState s_baseInitialization{&OnBaseInitializationEnter, &OnBaseInitializationUpdate, nullptr};
    static RED4ext::v1::GameState s_initialization{nullptr, &OnInitializationUpdate, nullptr};
    static RED4ext::v1::GameState s_running{&OnRunningEnter, &OnRunningUpdate, nullptr};
    static RED4ext::v1::GameState s_shutdown{nullptr, &OnShutdownUpdate, nullptr};

    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::BaseInitialization, &s_baseInitialization);
    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Initialization, &s_initialization);
    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Running, &s_running);
    aSdk->gameStates->Add(aHandle, RED4ext::EGameStateType::Shutdown, &s_shutdown);
}
} // namespace

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason, const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        CETHooking::Initialize(aHandle, aSdk);

        // Whatever happens, the plugin stays loaded (Main returns true): hooks attached before a failure must keep
        // pointing at mapped code. A failed start leaves CET inert.
        try
        {
            CET::Initialize();
            s_initialized = true;
        }
        catch (const std::exception& e)
        {
            // Options already logged the reason when the game build is not supported.
            if (auto logger = spdlog::default_logger())
                logger->error("Cyber Engine Tweaks did not start: {}", e.what());
        }
        catch (...)
        {
        }

        if (!s_initialized)
            break;

        // Before the game creates its window and event monitors, so CET sees input first.
        InputMac::InstallEarlyHooks();

        RegisterStates(aHandle, aSdk);
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        // The game has already destroyed its systems when plugins unload on macOS, so CET's objects (which hold game
        // handles) are left alone; mods got onShutdown during the Shutdown state. Only flush the logs.
        if (s_initialized)
        {
            spdlog::default_logger()->flush();
            if (auto logger = spdlog::get("scripting"))
                logger->flush();
        }
        break;
    }
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"Cyber Engine Tweaks";
    aInfo->author = L"CET team (macOS port)";
    aInfo->version = RED4EXT_V1_SEMVER(CET_VERSION_MAJOR, CET_VERSION_MINOR, CET_VERSION_PATCH);
    // RED4ext matches runtimes by Windows file version; CET checks the macOS game build (bundle version and LC_UUID)
    // itself in Options.
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
