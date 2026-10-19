#include "Hooking.h"

#include <atomic>

namespace
{
std::atomic<RED4ext::v1::PluginHandle> s_handle{nullptr};
std::atomic<const RED4ext::v1::Sdk*> s_sdk{nullptr};
} // namespace

void CETHooking::Initialize(RED4ext::v1::PluginHandle aHandle, const RED4ext::v1::Sdk* aSdk)
{
    s_handle = aHandle;
    s_sdk = aSdk;
}

RED4ext::v1::PluginHandle CETHooking::GetPluginHandle()
{
    return s_handle;
}

const RED4ext::v1::Sdk* CETHooking::GetSdk()
{
    return s_sdk;
}

bool CETHooking::Attach(void* aTarget, void* aDetour, void** aOriginal)
{
    const auto* sdk = s_sdk.load();
    if (!aTarget || !aDetour || !sdk || !sdk->hooking || !sdk->hooking->Attach)
        return false;

    return sdk->hooking->Attach(s_handle, aTarget, aDetour, aOriginal);
}

bool CETHooking::Detach(void* aTarget)
{
    const auto* sdk = s_sdk.load();
    if (!aTarget || !sdk || !sdk->hooking || !sdk->hooking->Detach)
        return false;

    return sdk->hooking->Detach(s_handle, aTarget);
}
