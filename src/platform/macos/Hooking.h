#pragma once

// macOS replacement for MinHook: game functions are hooked through RED4ext's hooking API (its arm64 hook engine),
// so CET never patches code itself. Calls into the game made through RED4ext.SDK resolve their addresses from
// RED4ext's verified address database; an address that is not verified resolves to null and the hook is skipped.

#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>

namespace CETHooking
{
// Called once from the plugin's Main (EMainReason::Load).
void Initialize(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk);

[[nodiscard]] RED4ext::v1::PluginHandle GetPluginHandle();
[[nodiscard]] const RED4ext::v1::Sdk* GetSdk();

// Hooks aTarget; aOriginal receives a callable pointer to the original function. Returns false (and leaves
// *aOriginal untouched) when aTarget is null or RED4ext refuses the hook.
bool Attach(void* aTarget, void* aDetour, void** aOriginal);
bool Detach(void* aTarget);

// Typed helper: Attach(pTarget, &Detour, &m_realFunction).
template <typename T> bool Attach(void* aTarget, T* aDetour, T** aOriginal)
{
    return Attach(aTarget, reinterpret_cast<void*>(aDetour), reinterpret_cast<void**>(aOriginal));
}
} // namespace CETHooking

// MinHook-compatible names, so CET's hook installation code stays the same on both platforms. A hook is attached
// (and enabled) by MH_CreateHook; MH_EnableHook/MH_DisableHook have nothing left to do.
using MH_STATUS = int;
inline constexpr MH_STATUS MH_OK = 0;
inline constexpr MH_STATUS MH_ERROR_NOT_EXECUTABLE = 1;

inline MH_STATUS MH_Initialize()
{
    return MH_OK;
}

inline MH_STATUS MH_Uninitialize()
{
    return MH_OK;
}

inline MH_STATUS MH_CreateHook(void* apTarget, void* apDetour, void** appOriginal)
{
    return CETHooking::Attach(apTarget, apDetour, appOriginal) ? MH_OK : MH_ERROR_NOT_EXECUTABLE;
}

inline MH_STATUS MH_EnableHook(void*)
{
    return MH_OK;
}

inline MH_STATUS MH_DisableHook(void*)
{
    return MH_OK;
}
