#include <stdafx.h>

#include "LuaVM.h"

#include <CET.h>
#include <Utils.h>

#include <reverse/RTTILocator.h>
#include <reverse/TweakDB/ResourcesList.h>

namespace
{

LuaVM* s_vm{nullptr};

using TOodleLZ_Decompress =
    size_t (*)(char* in, int insz, char* out, int outsz, int wantsFuzzSafety, int b, int c, void* d, void* e, void* f, void* g, void* workBuffer, size_t workBufferSize, int j);

struct Header
{
    uint32_t m_magic = 0;   // a hash of all types currently supported
    uint32_t m_version = 0; // 1
    uint32_t m_recordsCount = 0;
    uint32_t m_flatsCount = 0;
    uint32_t m_queriesCount = 0;
};

int32_t ReadCompressedInt(std::istream& aFile)
{
    int32_t uncompressed = 0;

    int8_t byte;
    aFile.read(reinterpret_cast<char*>(&byte), 1);
    uncompressed |= byte & 0x3F;
    if (byte & 0x40)
    {
        aFile.read(reinterpret_cast<char*>(&byte), 1);
        uncompressed |= (byte & 0x7F) << 6;
        if (byte & 0x80)
        {
            aFile.read(reinterpret_cast<char*>(&byte), 1);
            uncompressed |= (byte & 0x7F) << 13;
            if (byte & 0x80)
            {
                aFile.read(reinterpret_cast<char*>(&byte), 1);
                uncompressed |= (byte & 0x7F) << 20;
                if (byte & 0x80)
                {
                    aFile.read(reinterpret_cast<char*>(&byte), 1);
                    uncompressed |= byte << 27;
                }
            }
        }
    }

    return uncompressed;
}

void ReadTDBIDNameArray(std::istream& aFile, uint32_t aCount, TiltedPhoques::Map<uint64_t, TDBIDLookupEntry>& aOutMap)
{
    for (uint32_t i = 0; i < aCount; ++i)
    {
        const int32_t length = ReadCompressedInt(aFile);
        std::string str;
        str.resize(length);
        aFile.read(str.data(), length);
        aOutMap.try_emplace(TweakDBID(str).value, TDBIDLookupEntry{0, str});
    }
}

bool InitializeTweakDBMetadata(TiltedPhoques::Map<uint64_t, TDBIDLookupEntry>& lookup)
{
#ifdef __APPLE__
    // The macOS game links Oodle statically and does not export it. An uncompressed tweakdb.str in the tweakdb folder
    // still works; tweakdbstr.kark needs OodleLZ_Decompress and is skipped when it is not available.
    auto OodleLZ_Decompress = reinterpret_cast<TOodleLZ_Decompress>(dlsym(RTLD_DEFAULT, "OodleLZ_Decompress"));
#else
    auto hOodleHandle = GetModuleHandle(TEXT("oo2ext_7_win64.dll"));
    if (hOodleHandle == nullptr)
    {
        Log::Error("CDPRTweakDBMetadata::Initalize() - Could not get Oodle access");
        return false;
    }

    auto OodleLZ_Decompress = reinterpret_cast<TOodleLZ_Decompress>(GetProcAddress(hOodleHandle, "OodleLZ_Decompress"));
    if (OodleLZ_Decompress == nullptr)
    {
        Log::Error("CDPRTweakDBMetadata::Initalize() - Could not get OodleLZ_Decompress");
        return false;
    }
#endif

    const auto tdbstrPathRAW = GetAbsolutePath(L"tweakdb.str", CET::Get().GetPaths().TweakDB(), false, true);
    if (!tdbstrPathRAW.empty())
    {
        try
        {
            std::ifstream file(tdbstrPathRAW, std::ios::binary);
            file.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);
            Header header;
            file.read(reinterpret_cast<char*>(&header), sizeof(Header));
            assert(header.m_version == 1);
            ReadTDBIDNameArray(file, header.m_recordsCount, lookup);
            ReadTDBIDNameArray(file, header.m_flatsCount, lookup);
            ReadTDBIDNameArray(file, header.m_queriesCount, lookup);
        }
        // this is easier for now
        catch (std::exception&)
        {
            Log::Error("CDPRTweakDBMetadata::Initalize() - Failed to load tweakdb.str!");
            return false;
        }
    }
    else
    {
        if (OodleLZ_Decompress == nullptr)
        {
            Log::Warn("CDPRTweakDBMetadata::Initalize() - OodleLZ_Decompress is not available on this platform; place an uncompressed "
                      "tweakdb.str in the tweakdb folder to get TweakDB names");
            return false;
        }

        const auto tdbstrPath = GetAbsolutePath(L"tweakdbstr.kark", CET::Get().GetPaths().TweakDB(), false, true);
        if (tdbstrPath.empty())
        {
            Log::Error("CDPRTweakDBMetadata::Initalize() - Missing tweakdbstr.kark!");
            return false;
        }

        try
        {
            constexpr size_t headerSize = 8;
            auto encodedBytes = ReadWholeBinaryFile(tdbstrPath);

            std::string decodedBytes;
            decodedBytes.resize(reinterpret_cast<uint32_t*>(encodedBytes.data())[1]);

            // On the heap: 512 KiB is the whole stack of a secondary thread on macOS (this runs on one).
            std::vector<char> workingMemory(0x80000);
            auto size = OodleLZ_Decompress(
                encodedBytes.data() + headerSize, static_cast<int>(encodedBytes.size() - headerSize), decodedBytes.data(), static_cast<int>(decodedBytes.size()), 1, 1, 0, nullptr,
                nullptr, nullptr, nullptr, workingMemory.data(), workingMemory.size(), 3);

            assert(size == decodedBytes.size());
            if (size != decodedBytes.size())
            {
                Log::Error("CDPRTweakDBMetadata::Initalize() - Failed to decompress tweakdbstr.kark!");
                return false;
            }

            struct inline_buffer : std::streambuf
            {
                inline_buffer(char* base, std::ptrdiff_t n) { this->setg(base, base, base + n); }
            } tdbstrDecodedBuffer(decodedBytes.data(), decodedBytes.size());
            std::istream tdbstrDecodedFile(&tdbstrDecodedBuffer);
            tdbstrDecodedFile.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);

            Header header;
            tdbstrDecodedFile.read(reinterpret_cast<char*>(&header), sizeof(Header));
            assert(header.m_version == 1);
            ReadTDBIDNameArray(tdbstrDecodedFile, header.m_recordsCount, lookup);
            ReadTDBIDNameArray(tdbstrDecodedFile, header.m_flatsCount, lookup);
            ReadTDBIDNameArray(tdbstrDecodedFile, header.m_queriesCount, lookup);

            Log::Info("CDPRTweakDBMetadata::Initalize() - Primary TweakDB initialization successful!");
        }
        // this is easier for now
        catch (std::exception&)
        {
            Log::Error("CDPRTweakDBMetadata::Initalize() - Failed to load tweakdbstr.kark!");
            return false;
        }
    }

    // check if we have a tweakdb that was changed by REDmod
    const auto moddedTweakDbFilePath = GetAbsolutePath(L"tweakdb.str", CET::Get().GetPaths().R6CacheModdedRoot(), false, true);
    if (moddedTweakDbFilePath.empty())
        return true;

    try
    {
        std::ifstream file(moddedTweakDbFilePath, std::ios::binary);
        file.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);

        Header header;
        file.read(reinterpret_cast<char*>(&header), sizeof(Header));
        assert(header.m_version == 1);
        ReadTDBIDNameArray(file, header.m_recordsCount, lookup);
        ReadTDBIDNameArray(file, header.m_flatsCount, lookup);
        ReadTDBIDNameArray(file, header.m_queriesCount, lookup);
        Log::Info("CDPRTweakDBMetadata::Initalize() - REDMod TweakDB initialization successful!");
    }
    // this is easier for now
    // do not modify the return state since this isn't the main TweakDB
    catch (std::exception&)
    {
        Log::Warn("CDPRTweakDBMetadata::Initalize() - Failed to load REDMod TweakDB. Modded entries may not "
                  "be shown in the TweakDB Editor.");
    }

    return true;
}

} // namespace

void LuaVM::HookLog(RED4ext::IScriptable*, RED4ext::CStackFrame* apStack, void*, void*)
{
    static RTTILocator s_stringLocator("String");

    RED4ext::CString text{};

    apStack->data = nullptr;
    apStack->dataType = nullptr;

    RED4ext::ScriptRef<RED4ext::CString> ref;
    ref.innerType = s_stringLocator;
    ref.ref = &text;
    ref.hash = ref.innerType->GetName();

    apStack->currentParam++;
    const auto opcode = *apStack->code++;

    RED4ext::OpcodeHandlers::Run(opcode, apStack->context, apStack, &ref, &ref);
    apStack->code++; // skip ParamEnd

    spdlog::get("gamelog")->info("{}", ref.ref->c_str());
}

void LuaVM::HookLogChannel(RED4ext::IScriptable*, RED4ext::CStackFrame* apStack, void*, void*)
{
    static RTTILocator s_stringLocator("String");
    static RED4ext::CName s_debugChannel("DEBUG");
    static RED4ext::CName s_assertionChannel("ASSERT");

    RED4ext::CName channel;
    apStack->data = nullptr;
    apStack->dataType = nullptr;
    uint8_t opcode = *apStack->code++;
    apStack->currentParam++;

    RED4ext::OpcodeHandlers::Run(opcode, apStack->context, apStack, &channel, nullptr);

    RED4ext::CString text{};
    RED4ext::ScriptRef<RED4ext::CString> ref;
    ref.innerType = s_stringLocator;
    ref.ref = &text;
    ref.hash = ref.innerType->GetName();

    apStack->currentParam++;

    apStack->data = nullptr;
    apStack->dataType = nullptr;
    opcode = *apStack->code++;
    RED4ext::OpcodeHandlers::Run(opcode, apStack->context, apStack, &ref, &ref);

    apStack->code++; // skip ParamEnd

    if (channel == s_debugChannel)
        spdlog::get("scripting")->info("{}", ref.ref->c_str());

    std::string_view channelSV = channel.ToString();
    if (channelSV.empty())
        spdlog::get("gamelog")->info("[?{:X}] {}", channel.hash, ref.ref->c_str());
    else
    {
        if (channel == s_debugChannel)
            spdlog::get("gamelog")->debug("[{}] {}", channelSV, ref.ref->c_str());
        else if (channel == s_assertionChannel)
            spdlog::get("gamelog")->error("[{}] {}", channelSV, ref.ref->c_str());
        else
            spdlog::get("gamelog")->info("[{}] {}", channelSV, ref.ref->c_str());
    }
}

LuaVM::LuaVM(const Paths& aPaths, const Options& aOptions, VKBindings& aBindings, D3D12& aD3D12)
    : m_scripting(aPaths, aOptions, aBindings, aD3D12)
    , m_d3d12(aD3D12)
{
    Hook();

    // launch initialization of TweakDBID lookup and resource list on separate thread to not hog the game
    // resource list is currently only used by TweakDB Editor widget, it checks for initialization of this
    // before trying to access it or TweakDBID lookup
    // TweakDBID lookup is on top locked for the duration of the initialization so accessing it should not
    // be possible until it finishes

    aBindings.SetVM(this);
}

void LuaVM::Prepare()
{
    std::thread(
        [this]
        {
            {
                std::lock_guard _{m_tdbidLock};
                InitializeTweakDBMetadata(m_tdbidLookup);
            }

            ResourcesList::Get()->Initialize();
        })
        .detach();
}

#ifdef __APPLE__
uint64_t LuaVM::HookTDBIDCtorDerive(const TDBID* apBase, const char* acpName)
{
    const auto result = s_vm->m_realTDBIDCtorDerive(apBase, acpName);
    if (apBase && acpName)
        s_vm->RegisterTDBIDString(result, apBase->value, std::string(acpName));
    return result;
}
#else
TDBID* LuaVM::HookTDBIDCtorDerive(TDBID* apBase, TDBID* apThis, const char* acpName)
{
    const auto result = s_vm->m_realTDBIDCtorDerive(apBase, apThis, acpName);
    s_vm->RegisterTDBIDString(apThis->value, apBase->value, std::string(acpName));
    return result;
}
#endif

void LuaVM::HookTDBIDToStringDEBUG(RED4ext::IScriptable*, RED4ext::CStackFrame* apStack, void* apResult, void*)
{
    TDBID tdbid;
    apStack->data = nullptr;
    apStack->dataType = nullptr;
    apStack->currentParam++;
    const uint8_t opcode = *apStack->code++;
    RED4ext::OpcodeHandlers::Run(opcode, apStack->context, apStack, &tdbid, nullptr);
    apStack->code++; // skip ParamEnd

    if (apResult)
    {
        const std::string name = s_vm->GetTDBIDString(tdbid.value);
        const RED4ext::CString s(name.c_str());
        *static_cast<RED4ext::CString*>(apResult) = s;
    }
}

void LuaVM::HookTranslateBytecode(uintptr_t aBinder, uintptr_t aData)
{
    // Delay our lua hook until later, to ensure that Mod Organizer's VFS is hooked up.
    CET::Get().GetVM().Initialize();

    s_vm->m_realTranslateBytecode(aBinder, aData);
    s_vm->PostInitializeScripting();
}

uint64_t LuaVM::HookPlayerSpawned(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4)
{
    const auto ret = s_vm->m_realPlayerSpawned(a1, a2, a3, a4);

    if (!s_vm->m_initialized)
        s_vm->PostInitializeMods();

    return ret;
}

uint64_t LuaVM::HookTweakDBLoad(uintptr_t aThis, uintptr_t aParam)
{
    const auto ret = s_vm->m_realTweakDBLoad(aThis, aParam);

    // Disable changes tracking added to TweakDB in patch 2.0 and causing instability.
    RED4ext::TweakDB::Get()->unk160 = 0;

#ifdef __APPLE__
    // onTweak must come after onHook, as on Windows: TryPostInitialize runs PostInitializeScripting first if the
    // scripts are already bound, and otherwise defers both.
    s_vm->m_tweakDBLoaded = true;
    s_vm->TryPostInitialize();
#else
    s_vm->PostInitializeTweakDB();
#endif

    return ret;
}

void LuaVM::Hook()
{
    s_vm = this;

#ifdef _WIN32
    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_Log);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookLog), reinterpret_cast<void**>(&m_realLog)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::Log function!");
            else
                Log::Info("CScript::Log function hook complete!");
        }
    }

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_LogError);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookLog), reinterpret_cast<void**>(&m_realLogError)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::Log function!");
            else
                Log::Info("CScript::Log function hook complete!");
        }
    }

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_LogWarning);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookLog), reinterpret_cast<void**>(&m_realLogWarning)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::Log function!");
            else
                Log::Info("CScript::Log function hook complete!");
        }
    }

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_LogChannel);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookLogChannel), reinterpret_cast<void**>(&m_realLogChannel)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::LogChannel function!");
            else
                Log::Info("CScript::LogChannel function hook complete!");
        }
    }

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_LogChannelWarning);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookLogChannel), reinterpret_cast<void**>(&m_realLogChannelWarning)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::LogChannelWarning function!");
            else
                Log::Info("CScript::LogChannel function hook complete!");
        }
    }

#endif

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_TDBIDConstructorDerive);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<void*>(&HookTDBIDCtorDerive), reinterpret_cast<void**>(&m_realTDBIDCtorDerive)) != MH_OK ||
                MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::TDBIDConstructorDerive function!");
            else
                Log::Info("CScript::TDBIDConstructorDerive function hook complete!");
        }
    }

#ifdef _WIN32
    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_ToStringDEBUG);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<LPVOID>(HookTDBIDToStringDEBUG), reinterpret_cast<void**>(&m_realTDBIDToStringDEBUG)) != MH_OK ||
                MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::ToStringDEBUG function!");
            else
            {
                Log::Info("CScript::ToStringDEBUG function hook complete!");
            }
        }
    }

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_TranslateBytecode);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<LPVOID>(HookTranslateBytecode), reinterpret_cast<void**>(&m_realTranslateBytecode)) != MH_OK ||
                MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::TranslateBytecode function!");
            else
            {
                Log::Info("CScript::TranslateBytecode function hook complete!");
            }
        }
    }

#else
    HookInitializeScriptRuntime();
#endif

    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::CScript_TweakDBLoad);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<LPVOID>(HookTweakDBLoad), reinterpret_cast<void**>(&m_realTweakDBLoad)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook CScript::TweakDBLoad function!");
            else
            {
                Log::Info("CScript::TweakDBLoad function hook complete!");
            }
        }
    }

#ifdef _WIN32
    {
        const RED4ext::UniversalRelocPtr<uint8_t> func(CyberEngineTweaks::AddressHashes::PlayerSystem_OnPlayerSpawned);
        uint8_t* pLocation = func.GetAddr();

        if (pLocation)
        {
            if (MH_CreateHook(pLocation, reinterpret_cast<LPVOID>(HookPlayerSpawned), reinterpret_cast<void**>(&m_realPlayerSpawned)) != MH_OK || MH_EnableHook(pLocation) != MH_OK)
                Log::Error("Could not hook PlayerSystem::OnPlayerSpawned function!");
            else
            {
                Log::Info("PlayerSystem::OnPlayerSpawned function hook complete!");
            }
        }
    }

#endif

    GameMainThread::Get().AddRunningTask(
        [this]
        {
#ifdef __APPLE__
            s_vm->OnRunningUpdate();
#endif

            const auto cNow = std::chrono::high_resolution_clock::now();
            const auto cDelta = cNow - s_vm->m_lastframe;
            const auto cSeconds = std::chrono::duration_cast<std::chrono::duration<float>>(cDelta);

            s_vm->Update(cSeconds.count());

            s_vm->m_lastframe = cNow;

            return false;
        });

    GameMainThread::Get().AddShutdownTask(
        [this]
        {
            s_vm->m_scripting.UnloadAllMods();

            return true;
        });
}

#ifdef __APPLE__
namespace
{
using TInitializeScriptRuntime = void(RED4ext::CRTTISystem*);
TInitializeScriptRuntime* s_realInitializeScriptRuntime = nullptr;

// IRTTISystem::InitializeScriptRuntime is called by the script loader once the script bytecode is loaded and bound
// (RED4ext.SDK RTTISystem.hpp, vtable +0xF8). The RTTI system's destructors are its last two vtable slots, so the
// Itanium layout leaves this slot where the SDK declares it. Its neighbours were checked in the macOS 2.3.1 binary:
// slot 37 returns entry i of the string table at +0x1B0 (GetStringConst) and slot 38 assigns that table
// (SetStringTable), as the SDK declares.
constexpr size_t kInitializeScriptRuntimeSlot = 0xF8 / sizeof(void*);

void HookedInitializeScriptRuntime(RED4ext::CRTTISystem* apThis)
{
    s_realInitializeScriptRuntime(apThis);

    if (s_vm)
        s_vm->OnScriptRuntimeInitialized();
}
} // namespace

void LuaVM::HookInitializeScriptRuntime()
{
    auto* pRtti = RED4ext::CRTTISystem::Get();
    if (!pRtti || !*reinterpret_cast<void**>(pRtti))
    {
        Log::Error("IRTTISystem is not available; mods will be initialized when the game reaches its main loop.");
        return;
    }

    auto* pTarget = (*reinterpret_cast<void***>(pRtti))[kInitializeScriptRuntimeSlot];
    if (MH_CreateHook(pTarget, reinterpret_cast<void*>(&HookedInitializeScriptRuntime), reinterpret_cast<void**>(&s_realInitializeScriptRuntime)) != MH_OK)
        Log::Error("Could not hook IRTTISystem::InitializeScriptRuntime! Mods will be initialized when the game reaches its main loop.");
    else
        Log::Info("IRTTISystem::InitializeScriptRuntime function hook complete!");
}

bool LuaVM::AreScriptsBound()
{
    // PlayerPuppet::OnGameAttached is defined by the game's scripts, not natively: once RTTI has it, the script
    // bytecode is loaded and bound.
    auto* pRtti = RED4ext::CRTTISystem::Get();
    if (!pRtti)
        return false;

    const auto* pClass = pRtti->GetClass("PlayerPuppet");
    if (!pClass)
        return false;

    const RED4ext::CName cName("OnGameAttached");
    for (const auto* pFunc : pClass->funcs)
    {
        if (pFunc && pFunc->shortName == cName)
            return true;
    }
    return false;
}

void LuaVM::OnBaseInitialization()
{
    if (m_luaInitialized)
        return;

    m_luaInitialized = true;
    m_scripting.Initialize();
}

void LuaVM::OnScriptRuntimeInitialized()
{
    TryPostInitialize();
}

void LuaVM::TryPostInitialize()
{
    OnBaseInitialization();

    if (!m_scriptingPostInitialized)
    {
        if (!AreScriptsBound())
            return;

        PostInitializeScripting();
        m_scriptingPostInitialized = true;

        HookScriptLogHandlers();

        // Windows CET runs onInit when PlayerSystem reports the player's main object; the script-side equivalent is
        // PlayerPuppet::OnGameAttached. The flag is consumed on the next Running tick, outside the script call.
        m_scripting.ObservePlayerAttached([this] { m_playerAttached = true; });
    }

    if (m_tweakDBLoaded && !m_tweakDBPostInitialized)
    {
        PostInitializeTweakDB();
        m_tweakDBPostInitialized = true;
    }
}

void LuaVM::OnRunningEnter()
{
    // TweakDB is loaded during engine initialization. If its hook could not be attached, still give mods the TweakDB
    // API once the game runs.
    if (!m_tweakDBLoaded)
        m_tweakDBLoaded = RED4ext::TweakDB::Get() != nullptr;

    TryPostInitialize();
}

void LuaVM::OnRunningUpdate()
{
    if (!m_scriptingPostInitialized || !m_tweakDBPostInitialized)
        TryPostInitialize();

    if (!m_initialized && m_scriptingPostInitialized && m_tweakDBPostInitialized && m_playerAttached)
        PostInitializeMods();
}

void LuaVM::HookScriptLogHandlers()
{
    // Script natives are dispatched through RED4ext::OpcodeHandlers (a verified address): entry i of that table is the
    // handler of the native function whose registration index is i. Replacing entries redirects the script log
    // natives to CET's logs without patching game code. Their macOS addresses are not needed.
    const RED4ext::UniversalRelocPtr<RED4ext::OpcodeHandlers::Handler_t> cHandlers(RED4ext::Detail::AddressHashes::OpcodeHandlers);
    auto* pHandlers = cHandlers.GetAddr();
    auto* pRtti = RED4ext::CRTTISystem::Get();
    if (!pHandlers || !pRtti)
    {
        Log::Warn("Script log natives were not redirected (handler table unavailable).");
        return;
    }

    const auto swap = [pHandlers](RED4ext::CBaseFunction* apFunction, RED4ext::OpcodeHandlers::Handler_t aHook, RED4ext::OpcodeHandlers::Handler_t& aReal, const char* acpName)
    {
        // Natives registered at runtime get indices from 0xFF up; lower entries are the opcode handlers.
        const auto index = apFunction ? apFunction->GetRegIndex() : 0u;
        if (!apFunction || !apFunction->flags.isNative || index < 0xFF || !pHandlers[index])
        {
            Log::Warn("Script native {} was not redirected.", acpName);
            return;
        }

        aReal = pHandlers[index];
        pHandlers[index] = aHook;
        Log::Info("Script native {} redirected.", acpName);
    };

    swap(pRtti->GetFunction("Log"), reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookLog), m_realLog, "Log");
    swap(pRtti->GetFunction("LogError"), reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookLog), m_realLogError, "LogError");
    swap(pRtti->GetFunction("LogWarning"), reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookLog), m_realLogWarning, "LogWarning");
    swap(pRtti->GetFunction("LogChannel"), reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookLogChannel), m_realLogChannel, "LogChannel");
    swap(pRtti->GetFunction("LogChannelWarning"), reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookLogChannel), m_realLogChannelWarning, "LogChannelWarning");

    // TDBID.ToStringDEBUG is a static native of the class TDBID, which is registered under that native name
    // (0x102B79424); GetClassByScriptName finds only classes with a separate script name.
    RED4ext::CBaseFunction* pToStringDebug = nullptr;
    auto* pTDBIDClass = pRtti->GetClass("TDBID");
    if (!pTDBIDClass)
        pTDBIDClass = pRtti->GetClassByScriptName("TDBID");
    if (auto* pClass = pTDBIDClass)
    {
        for (auto* pFunc : pClass->staticFuncs)
        {
            if (pFunc && pFunc->shortName == RED4ext::CName("ToStringDEBUG"))
                pToStringDebug = pFunc;
        }
    }
    swap(pToStringDebug, reinterpret_cast<RED4ext::OpcodeHandlers::Handler_t>(&HookTDBIDToStringDEBUG), m_realTDBIDToStringDEBUG, "TDBID.ToStringDEBUG");
}
#endif
