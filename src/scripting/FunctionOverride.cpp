#include <stdafx.h>

#include "FunctionOverride.h"
#include "Scripting.h"
#include "Utils.h"
#include <reverse/WeakReference.h>
#include <reverse/RTTIHelper.h>
#ifdef _WIN32
#include <xbyak/xbyak.h>
#else
#include <fishhook.h>
#include <mach-o/loader.h>
#include <unordered_set>
#endif

namespace
{

FunctionOverride* s_pOverride = nullptr;

using TRunPureScriptFunction = bool (*)(RED4ext::CBaseFunction* apFunction, RED4ext::CScriptStack*, void*);
using TCreateFunction = void* (*)(void* apMemoryPool, size_t aSize);
using TCallScriptFunction = bool (*)(RED4ext::IFunction* apFunction, RED4ext::IScriptable* apContext, RED4ext::CStackFrame* apFrame, void* apOut, void* a4);

TRunPureScriptFunction RealRunPureScriptFunction = nullptr;
TCreateFunction RealCreateFunction = nullptr;
RED4ext::UniversalRelocFunc<TCallScriptFunction> CallScriptFunction(RED4ext::Detail::AddressHashes::CBaseFunction_InternalExecute);

constexpr size_t s_cMaxFunctionSize = std::max({sizeof(RED4ext::CClassFunction), sizeof(RED4ext::CClassStaticFunction), sizeof(RED4ext::CGlobalFunction)});

size_t GetFunctionSize(RED4ext::CBaseFunction* apFunction)
{
    if (apFunction->flags.isStatic)
    {
        return apFunction->flags.isNative ? sizeof(RED4ext::CClassStaticFunction) : sizeof(RED4ext::CGlobalFunction);
    }

    return sizeof(RED4ext::CClassFunction);
}

#ifdef _WIN32
struct OverrideCodegen : Xbyak::CodeGenerator
{
    OverrideCodegen(uintptr_t apRealFunc, uintptr_t apTrampoline)
    {
        sub(rsp, 56);
        mov(rax, apRealFunc);
        mov(ptr[rsp + 32], rax);
        mov(rax, apTrampoline);
        call(rax);
        add(rsp, 56);
        ret();
    }
};
#else
// The Windows build generates an x64 trampoline per overridden function at runtime (xbyak) that passes the function
// to HandleOverridenFunction. The macOS build uses a fixed pool of compiled thunks instead, each bound to one
// function through its index, so CET never creates executable memory.
constexpr size_t kThunkCount = 4096;
using TNativeScriptFunction = void (*)(RED4ext::IScriptable*, RED4ext::CStackFrame*, void*, int64_t);

std::array<std::atomic<RED4ext::CClassFunction*>, kThunkCount> s_thunkTargets{};
size_t s_nextThunk = 0; // guarded by FunctionOverride::m_lock (ApplyOverride runs with it held)

template <size_t... I> constexpr std::array<TNativeScriptFunction, sizeof...(I)> MakeThunkTable(std::index_sequence<I...>)
{
    return {&FunctionOverride::OverrideThunk<I>...};
}

// Swapping a function with its trampoline copies GetFunctionSize() bytes, up to sizeof(CClassFunction). The game
// allocates script functions (CScriptedFunction) and global natives with only 0xB8 bytes, so, like the Windows build
// (which hooks the game's function allocator, an address not verified on macOS), every function-sized allocation from
// the RTTI function pool is enlarged. The macOS game calls PoolStorageProxy<PoolRTTIFunction>::Allocate through its
// GOT (all 105 call sites in 2.3.1 use the stub), so the GOT entry is rebound; objects allocated before that are
// recognized and not overridden when they are too small.
struct Block
{
    void* memory;
    size_t size;
};
using TPoolAllocate = Block (*)(uint64_t aSize);

TPoolAllocate s_realPoolAllocate = nullptr;
std::mutex s_enlargedLock;
std::unordered_set<const void*> s_enlarged;

Block HookedPoolAllocate(uint64_t aSize)
{
    if (aSize >= sizeof(RED4ext::CScriptedFunction) && aSize < s_cMaxFunctionSize)
    {
        const auto block = s_realPoolAllocate(s_cMaxFunctionSize);
        if (block.memory)
        {
            std::lock_guard _(s_enlargedLock);
            s_enlarged.insert(block.memory);
        }
        return block;
    }
    return s_realPoolAllocate(aSize);
}

// Allocation size of an existing function object: class functions (member or static) are allocated with
// sizeof(CClassFunction) by the game; other kinds only when they were enlarged above.
size_t GetAllocatedFunctionSize(RED4ext::CBaseFunction* apFunction, const void* apClassFunctionVtbl)
{
    if (*reinterpret_cast<void**>(apFunction) == apClassFunctionVtbl)
        return sizeof(RED4ext::CClassFunction);

    std::lock_guard _(s_enlargedLock);
    return s_enlarged.contains(apFunction) ? s_cMaxFunctionSize : sizeof(RED4ext::CScriptedFunction);
}

void ProbeHandler(RED4ext::IScriptable*, RED4ext::CStackFrame* apFrame, void*, int64_t)
{
    apFrame->code++;
}
#endif

} // namespace

#ifdef __APPLE__
template <size_t N> void FunctionOverride::OverrideThunk(RED4ext::IScriptable* apContext, RED4ext::CStackFrame* apFrame, void* apOut, int64_t a4)
{
    HandleOverridenFunction(apContext, apFrame, apOut, reinterpret_cast<void*>(a4), s_thunkTargets[N].load(std::memory_order_acquire));
}

namespace
{
const std::array<TNativeScriptFunction, kThunkCount> s_thunks = MakeThunkTable(std::make_index_sequence<kThunkCount>{});
}

void FunctionOverride::HookFunctionAllocator()
{
    const auto* header = Platform::GetGameImageHeader();
    rebinding rebind{"_ZN3red6memory16PoolStorageProxyINS_16PoolRTTIFunctionEE8AllocateEy", reinterpret_cast<void*>(&HookedPoolAllocate),
                     reinterpret_cast<void**>(&s_realPoolAllocate)};
    if (!header || rebind_symbols_image(const_cast<mach_header_64*>(header), Platform::GetGameImageSlide(), &rebind, 1) != 0 || !s_realPoolAllocate)
        Log::Error("Could not hook the RTTI function allocator! Overrides of script functions will be refused.");
    else
        Log::Info("RTTI function allocator hook complete!");
}
#endif

FunctionOverride::FunctionOverride(Scripting* apScripting)
    : m_pScripting(apScripting)
{
    s_pOverride = this;

#ifndef __APPLE__
    m_pBuffer = m_pBufferStart = VirtualAlloc(nullptr, m_size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#endif

    Hook();
}

FunctionOverride::~FunctionOverride()
{
#ifndef __APPLE__
    VirtualFree(m_pBufferStart, 0, MEM_RELEASE);
#endif

    s_pOverride = nullptr;
}

#ifndef __APPLE__
void* FunctionOverride::MakeExecutable(const uint8_t* apData, size_t aSize)
{
    if (std::align(0x10, aSize, m_pBuffer, m_size))
    {
        auto* result = static_cast<uint8_t*>(m_pBuffer);
        m_pBuffer = static_cast<char*>(m_pBuffer) + aSize;
        m_size -= aSize;

        std::memcpy(result, apData, aSize);

        return result;
    }

    return nullptr;
}
#endif

void FunctionOverride::Refresh()
{
    for (auto& [pFunction, pContext] : s_pOverride->m_functions)
    {
        CopyFunctionDescription(pContext.Trampoline, pFunction, pContext.Trampoline->flags.isNative);
    }
}

void FunctionOverride::Clear()
{
    {
        std::lock_guard pendingLock(m_pendingLock);
        m_pending.clear();
    }

    std::lock_guard lock(s_pOverride->m_lock);

    // Reverse order as we want to swap from most recent to oldest change
    for (auto& [pFunction, pContext] : m_functions)
    {
        auto* pRealFunction = pContext.Trampoline;

        size_t funcSize = GetFunctionSize(pRealFunction);

        // TODO - undefined behaviour!
        std::array<char, s_cMaxFunctionSize> tmpBuffer{};
        std::memcpy(&tmpBuffer, pRealFunction, funcSize);
        std::memcpy(pRealFunction, pFunction, funcSize);
        std::memcpy(pFunction, &tmpBuffer, funcSize);
    }

    m_functions.clear();
}

#ifndef __APPLE__
void* FunctionOverride::HookCreateFunction(void* apMemoryPool, size_t)
{
    enum
    {
        kOverrideAllocationSize = std::max(s_cMaxFunctionSize, sizeof(RED4ext::CScriptedFunction))
    };

    return RealCreateFunction(apMemoryPool, kOverrideAllocationSize);
}
#endif

bool FunctionOverride::HookRunPureScriptFunction(RED4ext::CClassFunction* apFunction, RED4ext::CScriptStack* apStack, RED4ext::CStackFrame* a3)
{
    if (apFunction->flags.isNative == 1 && s_pOverride)
    {
        std::shared_lock lock(s_pOverride->m_lock);

        const auto itor = s_pOverride->m_functions.find(apFunction);
        if (itor == std::end(s_pOverride->m_functions))
        {
            spdlog::get("scripting")->error("Function appears to be hooked but isn't?");
            return false;
        }

        const auto& chain = itor->second;

        if (!chain.IsEmpty)
        {
            TiltedPhoques::StackAllocator<1 << 13> s_allocator;

            const auto pAllocator = TiltedPhoques::Allocator::Get();
            TiltedPhoques::Allocator::Set(&s_allocator);
            TiltedPhoques::Vector<sol::object> args(0);
            TiltedPhoques::Vector<RED4ext::CStackType> outArgs;
            TiltedPhoques::Allocator::Set(pAllocator);

            RED4ext::CStackType ret;
            ret.value = apStack->GetResultAddr();
            ret.type = apStack->GetResultType();

            const auto pContext = apStack->GetContext();

            {
                auto lockedState = chain.pScripting->GetLockedState();
                const auto& luaState = lockedState.Get();

                if (!apFunction->flags.isStatic && pContext)
                {
                    args.reserve(RedSize(apFunction->params) + 1);

                    const auto weak = RED4ext::WeakHandle(*reinterpret_cast<RED4ext::WeakHandle<RED4ext::IScriptable>*>(&pContext->ref));
                    args.emplace_back(make_object(luaState, WeakReference(lockedState, weak)));
                }
                else
                {
                    args.reserve(RedSize(apFunction->params));
                }

                // There are at least two different implementations of CScriptStack.
                // They have different structures for args and return value.
                // The correct way to retieve args is to use a virtual method,
                // but it's inefficient because it allocates another stack.
                // This simple check allows us to avoid copying and read the args directly.
                const bool isArrayArgs = apStack->value != ret.value;

                for (uint32_t i = 0; i < RedSize(apFunction->params); ++i)
                {
                    const auto& pParam = apFunction->params[i];

                    RED4ext::CStackType arg;
                    arg.type = pParam->type;

                    if (isArrayArgs)
                        arg.value = reinterpret_cast<RED4ext::CStackType*>(apStack->args)[i].value;
                    else
                        arg.value = apStack->args + pParam->valueOffset;

                    args.emplace_back(Scripting::ToLua(lockedState, arg));

                    if (pParam->flags.isOut)
                        outArgs.emplace_back(arg);
                }
            }

            return ExecuteChain(chain, lock, pContext, &args, &ret, &outArgs, apStack, a3, nullptr, 0);
        }

        if (chain.CollectGarbage)
            s_pOverride->m_pScripting->CollectGarbage();

        auto* pRealFunction = chain.Trampoline;

        lock.unlock();

        return RealRunPureScriptFunction(pRealFunction, apStack, a3);
    }

    return RealRunPureScriptFunction(apFunction, apStack, a3);
}

void FunctionOverride::HandleOverridenFunction(RED4ext::IScriptable* apContext, RED4ext::CStackFrame* apFrame, void* apOut, void* a4, RED4ext::CClassFunction* apFunction)
{
    if (!s_pOverride)
        return;

    std::shared_lock lock(s_pOverride->m_lock);

    const auto itor = s_pOverride->m_functions.find(apFunction);
    if (itor == std::end(s_pOverride->m_functions))
    {
        spdlog::get("scripting")->error("Function appears to be hooked but isn't?");
        return;
    }

    const auto& chain = itor->second;

    // Save state so we can rollback to it after we popped for ourself
    auto* pCode = apFrame->code;
    const uint8_t currentParam = apFrame->currentParam;

    if (!chain.IsEmpty)
    {
        // Cheap allocation
        TiltedPhoques::StackAllocator<1 << 13> s_allocator;

        const auto pAllocator = TiltedPhoques::Allocator::Get();
        TiltedPhoques::Allocator::Set(&s_allocator);
        TiltedPhoques::Vector<sol::object> args(0);
        TiltedPhoques::Vector<RED4ext::CStackType> outArgs;
        TiltedPhoques::Allocator::Set(pAllocator);

        {
            auto lockedState = chain.pScripting->GetLockedState();
            const auto& luaState = lockedState.Get();

            if (!apFunction->flags.isStatic)
            {
                args.reserve(RedSize(apFunction->params) + 1);

                const auto pContext = apContext ? apContext : apFrame->context;
                if (pContext)
                {
                    const auto ref = reinterpret_cast<RED4ext::WeakHandle<RED4ext::IScriptable>*>(&pContext->ref);
                    const auto weak = RED4ext::WeakHandle(*ref);
                    args.emplace_back(make_object(luaState, WeakReference(lockedState, weak)));
                }
                else
                {
                    args.emplace_back(sol::lua_nil);
                }
            }
            else
            {
                args.reserve(RedSize(apFunction->params));
            }

            // Nasty way of popping all args
            for (const auto& pArg : apFunction->params)
            {
                auto* pType = pArg->type;
                auto* pTypeAllocator = pType->GetAllocator();

                auto* pInstance = pTypeAllocator->AllocAligned(pType->GetSize(), pType->GetAlignment()).memory;
                std::memset(pInstance, 0, pType->GetSize());
                pType->Construct(pInstance);

                const bool isScriptRef = pArg->type->GetType() == RED4ext::ERTTIType::ScriptReference;

                // Determines if script ref was created from rvalue and other side expects us to copy it.
                // If true, we must allocate the memory for the value and initialize our own script ref.
                // If false, we don't have to initialize anything on our side and pInstance will be filled
                // with a pointer to the original value.
                const bool isTemporary = (apFrame->paramFlags >> apFrame->currentParam) & 1;

                if (isScriptRef && isTemporary)
                {
                    auto* pInnerType = static_cast<RED4ext::CRTTIScriptReferenceType*>(pType)->innerType;
                    auto* pScriptRef = static_cast<RED4ext::ScriptRef<void>*>(pInstance);
                    pScriptRef->innerType = pInnerType;
                    pScriptRef->hash = pInnerType->GetName();
                    pScriptRef->ref = pInnerType->GetAllocator()->AllocAligned(pInnerType->GetSize(), pInnerType->GetAlignment()).memory;
                    pInnerType->Construct(pScriptRef->ref);
                }

                RED4ext::CStackType arg;
                arg.type = pArg->type;
                arg.value = pInstance;

                apFrame->currentParam++;
                apFrame->data = nullptr;
                apFrame->dataType = nullptr;
                const auto opcode = *apFrame->code++;
                RED4ext::OpcodeHandlers::Run(opcode, apFrame->context, apFrame, pInstance, isScriptRef ? pInstance : nullptr);

                args.emplace_back(Scripting::ToLua(lockedState, arg));

                if (pArg->flags.isOut)
                {
                    // This is an original arg, pInstance contains copy
                    if (apFrame->data)
                        arg.value = apFrame->data;

                    outArgs.emplace_back(arg);
                }

                // Release inner values
                if (isScriptRef && isTemporary)
                {
                    auto* pScriptRef = static_cast<RED4ext::ScriptRef<void>*>(pInstance);
                    pScriptRef->innerType->Destruct(pScriptRef->ref);
                    pScriptRef->innerType->GetAllocator()->Free(pScriptRef->ref);
                    pScriptRef->ref = nullptr;
                }

                if (!pArg->flags.isOut || apFrame->data)
                {
                    pType->Destruct(pInstance);
                    pTypeAllocator->Free(pInstance);
                }
            }
        }

        apFrame->code++; // skip ParamEnd

        RED4ext::CStackType ret;

        if (apFunction->returnType)
        {
            ret.type = apFunction->returnType->type;
            ret.value = apOut;
        }

        ExecuteChain(chain, lock, apContext, &args, &ret, &outArgs, nullptr, apFrame, pCode, currentParam);
        return;
    }

    if (chain.CollectGarbage)
        s_pOverride->m_pScripting->CollectGarbage();

    auto* pRealFunction = chain.Trampoline;

    lock.unlock();

    CallScriptFunction(pRealFunction, apContext, apFrame, apOut, a4);
}

bool FunctionOverride::ExecuteChain(
    const CallChain& aChain, std::shared_lock<std::shared_mutex>& aLock, RED4ext::IScriptable* apContext, TiltedPhoques::Vector<sol::object>* apOrigArgs,
    RED4ext::CStackType* apResult, TiltedPhoques::Vector<RED4ext::CStackType>* apOutArgs, RED4ext::CScriptStack* apStack, RED4ext::CStackFrame* apFrame, char* apCode,
    uint8_t aParam)
{
    if (!aChain.Before.empty())
    {
        auto lockedState = aChain.pScripting->GetLockedState();

        for (const auto& call : aChain.Before)
        {
            const auto result = call->ScriptFunction(as_args(*apOrigArgs));

            if (!result.valid())
            {
                const auto logger = call->Environment["__logger"].get<std::shared_ptr<spdlog::logger>>();
                logger->error(result.get<sol::error>().what());
            }
        }
    }

    auto* pRealFunction = aChain.Trampoline;
    bool realRetValue = true;

    if (!aChain.Overrides.empty())
    {
        auto lockedState = aChain.pScripting->GetLockedState();
        auto& luaState = lockedState.Get();

        sol::object luaContext = pRealFunction->flags.isStatic ? sol::lua_nil : apOrigArgs->at(0);
        TiltedPhoques::Vector<sol::object> luaArgs(apOrigArgs->begin() + (pRealFunction->flags.isStatic ? 0 : 1), apOrigArgs->end());

        const auto luaWrapped = WrapNextOverride(aChain, 0, luaState, luaContext, luaArgs, pRealFunction, apContext, aLock);
        const auto luaResult = luaWrapped(as_args(luaArgs));

        if (luaResult.valid())
        {
            auto luaRetOffset = 0;

            if (apResult && apResult->value)
            {
                Scripting::ToRED(luaResult.get<sol::object>(), *apResult);
                ++luaRetOffset;
            }

            if (apOutArgs && !apOutArgs->empty())
            {
                for (auto i = 0; i < static_cast<int>(apOutArgs->size()); ++i)
                {
                    auto luaOutArg = luaResult.get<sol::object>(i + luaRetOffset);

                    if (luaOutArg != sol::lua_nil)
                        Scripting::ToRED(luaOutArg, apOutArgs->at(i));
                }
            }
        }
    }
    else
    {
        if (apStack != nullptr)
        {
            realRetValue = RealRunPureScriptFunction(pRealFunction, apStack, apFrame);
        }
        else
        {
            apFrame->code = apCode;
            apFrame->currentParam = aParam;

            realRetValue = CallScriptFunction(pRealFunction, apContext, apFrame, apResult->value, apResult->type);
        }
    }

    if (!aChain.After.empty())
    {
        auto lockedState = aChain.pScripting->GetLockedState();

        for (const auto& call : aChain.After)
        {
            const auto result = call->ScriptFunction(as_args(*apOrigArgs));

            if (!result.valid())
            {
                const auto logger = call->Environment["__logger"].get<std::shared_ptr<spdlog::logger>>();
                logger->error(result.get<sol::error>().what());
            }
        }
    }

    if (apOrigArgs && !apOrigArgs->empty())
    {
        auto lockedState = aChain.pScripting->GetLockedState();
        apOrigArgs->resize(0);
    }

    if (aChain.CollectGarbage)
        s_pOverride->m_pScripting->CollectGarbage();

    return realRetValue;
}

sol::function FunctionOverride::WrapNextOverride(
    const CallChain& aChain, int aStep, sol::state& aLuaState, sol::object& aLuaContext, TiltedPhoques::Vector<sol::object>& aLuaArgs, RED4ext::CBaseFunction* apRealFunction,
    RED4ext::IScriptable* apRealContext, std::shared_lock<std::shared_mutex>& aLock)
{
    if (aStep == static_cast<int>(aChain.Overrides.size()))
    {
        // The pointers are captured by value: these functions run after WrapNextOverride has returned, and on arm64 its
        // parameters live in its own stack frame (on Windows x64 they sit in the caller's home space, which hid this).
        return MakeSolFunction(
            aLuaState,
            [&, apRealFunction, apRealContext](sol::variadic_args aWrapArgs, sol::this_state aState) -> sol::variadic_results
            {
                std::string errorMessage;
                sol::variadic_results results = RTTIHelper::Get().ExecuteFunction(apRealFunction, apRealContext, aWrapArgs, 0, errorMessage);

                if (!errorMessage.empty())
                {
                    const sol::environment cEnv = aChain.Overrides.front()->Environment;
                    const auto logger = cEnv["__logger"].get<std::shared_ptr<spdlog::logger>>();
                    logger->error("Error: {}", errorMessage);

                    aLock.unlock();

                    luaL_error(aState, errorMessage.c_str());
                    return {};
                }

                return results;
            });
    }

    return MakeSolFunction(
        aLuaState,
        [&, aStep, apRealFunction, apRealContext](sol::variadic_args aWrapArgs, sol::this_state aState) -> sol::variadic_results
        {
            const auto call = (aChain.Overrides.rbegin() + aStep)->get();

            for (uint32_t i = 0; i < RedSize(apRealFunction->params); ++i)
            {
                if (static_cast<int>(i) < aWrapArgs.leftover_count())
                    aLuaArgs[i] = aWrapArgs[i];
                else
                    aLuaArgs[i] = static_cast<sol::object>(sol::lua_nil);
            }

            auto next = WrapNextOverride(aChain, aStep + 1, aLuaState, aLuaContext, aLuaArgs, apRealFunction, apRealContext, aLock);
            auto result = aLuaContext == sol::lua_nil ? call->ScriptFunction(as_args(aLuaArgs), next) : call->ScriptFunction(aLuaContext, as_args(aLuaArgs), next);

            if (!result.valid())
            {
                const sol::environment cEnv = call->Environment;
                const auto logger = cEnv["__logger"].get<std::shared_ptr<spdlog::logger>>();
                logger->error(result.get<sol::error>().what());

                aLock.unlock();

                luaL_error(aState, result.get<sol::error>().what());
                return {};
            }

            sol::variadic_results results;
            for (auto&& element : result)
                results.emplace_back(element);

            return results;
        });
}

void FunctionOverride::Hook() const
{
    {
        const RED4ext::UniversalRelocPtr<void> func(CyberEngineTweaks::AddressHashes::CScript_RunPureScript);
        RealRunPureScriptFunction = reinterpret_cast<TRunPureScriptFunction>(func.GetAddr());
        if (!RealRunPureScriptFunction)
            Log::Error("Could not find pure run script function!");
        else
        {
            auto* pLocation = RealRunPureScriptFunction;
            if (MH_CreateHook(reinterpret_cast<LPVOID>(pLocation), reinterpret_cast<LPVOID>(HookRunPureScriptFunction), reinterpret_cast<void**>(&RealRunPureScriptFunction)) !=
                    MH_OK ||
                MH_EnableHook(reinterpret_cast<LPVOID>(pLocation)) != MH_OK)
                Log::Error("Could not hook RealRunScriptFunction function!");
            else
                Log::Info("RealRunScriptFunction function hook complete!");
        }
    }

#ifdef __APPLE__
    // CET is constructed at plugin load, before the game loads its scripts.
    HookFunctionAllocator();
#else
    {
        const RED4ext::UniversalRelocPtr<void> func(CyberEngineTweaks::AddressHashes::CScript_AllocateFunction);
        RealCreateFunction = reinterpret_cast<TCreateFunction>(func.GetAddr());
        if (!RealCreateFunction)
            Log::Error("Could not find create function!");
        else
        {
            auto* pLocation = RealCreateFunction;
            if (MH_CreateHook(reinterpret_cast<LPVOID>(pLocation), reinterpret_cast<LPVOID>(HookCreateFunction), reinterpret_cast<void**>(&RealCreateFunction)) != MH_OK ||
                MH_EnableHook(reinterpret_cast<LPVOID>(pLocation)) != MH_OK)
                Log::Error("Could not hook RealCreateFunction function!");
            else
                Log::Info("RealCreateFunction function hook complete!");
        }
    }
#endif
}

void FunctionOverride::Override(
    const std::string& acTypeName, const std::string& acFullName, sol::protected_function aFunction, sol::environment aEnvironment, bool aAbsolute, bool aAfter,
    bool aCollectGarbage)
{
    auto* pRtti = RED4ext::CRTTISystem::Get();
    auto* pClassType = pRtti->GetClass(acTypeName.c_str());

    if (!pClassType)
    {
        const auto* pNativeCName = pRtti->scriptToNative.Get(RED4ext::CName(acTypeName.c_str()));

        if (!pNativeCName)
        {
            spdlog::get("scripting")->error("Class type {} not found", acTypeName);
            return;
        }

        pClassType = pRtti->GetClass(*pNativeCName);
    }

    // Get the real function
    auto* pRealFunction = pClassType->GetFunction(acFullName.c_str());

    if (!pRealFunction)
    {
        pRealFunction = reinterpret_cast<RED4ext::CClassFunction*>(RTTIHelper::Get().FindFunction(pClassType, RED4ext::FNV1a64(acFullName.c_str())));

        if (!pRealFunction)
        {
            spdlog::get("scripting")->error("Function {} in class {} does not exist", acFullName, acTypeName);
            return;
        }
    }

    // Hooked functions can run on worker threads. They hold m_lock (shared) while they wait for the Lua state lock.
    // Override() is called from Lua, i.e. with the Lua state locked, so blocking on m_lock here can deadlock (lock order
    // inversion), e.g. when a mod registers an Observe from onUpdate while a hooked function runs on a job thread.
    // Only try the lock: if it is busy, queue the registration; ApplyPendingOverrides() applies it on the next update.
    std::unique_lock lock(m_lock, std::try_to_lock);
    std::lock_guard pendingLock(m_pendingLock);
    if (lock.owns_lock() && m_pending.empty())
    {
        ApplyOverride(pClassType, pRealFunction, acFullName, std::move(aFunction), aEnvironment, aAbsolute, aAfter, aCollectGarbage);
        return;
    }

    m_pending.push_back({pClassType, pRealFunction, acFullName, std::move(aFunction), aEnvironment, aAbsolute, aAfter, aCollectGarbage});
    spdlog::get("scripting")->debug("Deferred override of {}::{} (function lock busy)", acTypeName, acFullName);
}

void FunctionOverride::ApplyPendingOverrides()
{
    // Called with the Lua state locked, so never block on m_lock here either: retry on the next update instead.
    std::unique_lock lock(m_lock, std::try_to_lock);
    if (!lock.owns_lock())
        return;

    std::lock_guard pendingLock(m_pendingLock);
    for (auto& pending : m_pending)
    {
        ApplyOverride(
            pending.pClassType, pending.pRealFunction, pending.FullName, std::move(pending.Function), pending.Environment, pending.Absolute, pending.After, pending.CollectGarbage);
    }
    m_pending.clear();
}

void FunctionOverride::ApplyOverride(
    RED4ext::CClass* pClassType, RED4ext::CClassFunction* pRealFunction, const std::string& acFullName, sol::protected_function aFunction, sol::environment aEnvironment,
    bool aAbsolute, bool aAfter, bool aCollectGarbage)
{
    CallChain* pEntry = nullptr;
    const auto itor = m_functions.find(pRealFunction);

    // This function was never hooked
    if (itor == std::end(m_functions))
    {
        m_functions[pRealFunction] = {};
        pEntry = &m_functions[pRealFunction];

        RED4ext::CBaseFunction* pFunc;

        if (!m_trampolines.contains(pRealFunction))
        {
#ifdef __APPLE__
            // The swap below copies GetFunctionSize() bytes into the real function object; refuse objects the game
            // allocated smaller than that (see HookedPoolAllocate).
            static const void* s_classFunctionVtbl = [pClassType]
            {
                auto* pProbe = RED4ext::CClassFunction::Create(pClassType, "__cet_override_probe", "__cet_override_probe", &ProbeHandler);
                return *reinterpret_cast<void**>(pProbe); // the probe is never registered; it is leaked on purpose
            }();

            if (GetAllocatedFunctionSize(pRealFunction, s_classFunctionVtbl) < GetFunctionSize(pRealFunction))
            {
                spdlog::get("scripting")->error("Cannot override {}::{}: the game allocated this function before CET could prepare it", pClassType->GetName().ToString(), acFullName);
                m_functions.erase(pRealFunction);
                return;
            }

            if (s_nextThunk >= kThunkCount)
            {
                spdlog::get("scripting")->error("Cannot override {}::{}: all {} override slots are in use", pClassType->GetName().ToString(), acFullName, kThunkCount);
                m_functions.erase(pRealFunction);
                return;
            }

            const auto thunkIndex = s_nextThunk++;
            s_thunkTargets[thunkIndex].store(pRealFunction, std::memory_order_release);
            auto* pExecutablePayload = s_thunks[thunkIndex];
#else
            const auto funcAddr = reinterpret_cast<uintptr_t>(&FunctionOverride::HandleOverridenFunction);

            const OverrideCodegen codegen(reinterpret_cast<uintptr_t>(pRealFunction), funcAddr);

            using TNativeScriptFunction = void (*)(RED4ext::IScriptable*, RED4ext::CStackFrame*, void*, int64_t);
            auto* pExecutablePayload = reinterpret_cast<TNativeScriptFunction>(MakeExecutable(codegen.getCode(), codegen.getSize()));
#endif

            if (pRealFunction->flags.isStatic)
            {
                if (pRealFunction->flags.isNative)
                {
                    auto* pResult = RED4ext::CClassStaticFunction::Create(pClassType, acFullName.c_str(), acFullName.c_str(), pExecutablePayload, pRealFunction->flags);
                    pResult->parent = pRealFunction->parent;
                    pFunc = pResult;
                }
                else
                {
                    pFunc = RED4ext::CGlobalFunction::Create(acFullName.c_str(), acFullName.c_str(), pExecutablePayload);
                }
            }
            else
            {
                auto* pResult = RED4ext::CClassFunction::Create(pClassType, acFullName.c_str(), acFullName.c_str(), pExecutablePayload, pRealFunction->flags);
                pResult->parent = pRealFunction->parent;
                pFunc = pResult;
            }

            CopyFunctionDescription(pFunc, pRealFunction, true);

            m_trampolines[pRealFunction] = pFunc;
        }
        else
        {
            pFunc = m_trampolines[pRealFunction];
        }

        pEntry->Trampoline = pFunc;
        pEntry->pScripting = m_pScripting;
        pEntry->CollectGarbage = aCollectGarbage;
        pEntry->IsEmpty = true;

        // Swap the content of the real function with the one we just created
        const size_t funcSize = GetFunctionSize(pRealFunction);

        // TODO - undefined behaviour!
        std::array<char, s_cMaxFunctionSize> tmpBuffer{};
        std::memcpy(&tmpBuffer, pRealFunction, funcSize);
        std::memcpy(pRealFunction, pFunc, funcSize);
        std::memcpy(pFunc, &tmpBuffer, funcSize);
    }
    else
    {
        pEntry = &itor.value();
    }

    if (aFunction != sol::lua_nil)
    {
        auto pContext = TiltedPhoques::MakeUnique<Context>();
        pContext->ScriptFunction = std::move(aFunction);
        pContext->Environment = aEnvironment;
        pContext->Forward = !aAbsolute;

        if (aAbsolute)
            pEntry->Overrides.emplace_back(std::move(pContext));
        else if (aAfter)
            pEntry->After.emplace_back(std::move(pContext));
        else
            pEntry->Before.emplace_back(std::move(pContext));

        if (pEntry->IsEmpty)
            pEntry->IsEmpty = false;
    }
}

void FunctionOverride::CopyFunctionDescription(RED4ext::CBaseFunction* aFunc, RED4ext::CBaseFunction* aRealFunc, bool aForceNative)
{
    aFunc->fullName = aRealFunc->fullName;
    aFunc->shortName = aRealFunc->shortName;

    aFunc->returnType = aRealFunc->returnType;

    aFunc->params.Clear();
    for (auto* p : aRealFunc->params)
    {
        aFunc->params.PushBack(p);
    }

    aFunc->localVars.Clear();
    for (auto* p : aRealFunc->localVars)
    {
        aFunc->localVars.PushBack(p);
    }

    aFunc->unk20 = aRealFunc->unk20;
    aFunc->unk48 = aRealFunc->unk48;
    aFunc->unkAC = aRealFunc->unkAC;

    aFunc->flags = aRealFunc->flags;
    aFunc->flags.isNative = aForceNative;

    std::memcpy(&aFunc->bytecode, &aRealFunc->bytecode, sizeof(aRealFunc->bytecode));
}
