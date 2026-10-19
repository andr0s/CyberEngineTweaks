#pragma once

struct GameMainThread
{
    static GameMainThread& Get();

    void AddBaseInitializationTask(const std::function<bool()>& aFunction);
    void AddInitializationTask(const std::function<bool()>& aFunction);
    void AddRunningTask(const std::function<bool()>& aFunction);
    void AddShutdownTask(const std::function<bool()>& aFunction);
    void AddGenericTask(const std::function<bool()>& aFunction);

#ifdef __APPLE__
    // RED4ext calls this from its game-state callbacks (see platform/macos/PluginMain.cpp) on the game's main thread,
    // once per tick of the given state. Returns true when the state's task queue is empty.
    bool DrainState(RED4ext::EGameStateType aState);
#endif

private:
    GameMainThread() = default;

    using TStateTick = bool(RED4ext::IGameState*, RED4ext::CGameApplication*);

    static bool HookStateTick(RED4ext::IGameState* apThisState, RED4ext::CGameApplication* apGameApplication);

    // helper task queue which executes added tasks each drain until they are finished
    struct RepeatedTaskQueue
    {
        void AddTask(const std::function<bool()>& aFunction);
        void Drain();
        bool Empty();

    private:
        std::recursive_mutex m_mutex;
        TiltedPhoques::Vector<std::function<bool()>> m_tasks;
    };

#ifdef __APPLE__
    std::array<RepeatedTaskQueue, 4> m_stateTasks;
#else
    struct StateTickOverride
    {
        StateTickOverride(uint32_t aHash, const char* acpRealFunctionName);
        ~StateTickOverride();

        bool OnTick(RED4ext::IGameState*, RED4ext::CGameApplication*);

        uint8_t* Location = nullptr;
        TStateTick* RealFunction = nullptr;
        RepeatedTaskQueue Tasks;
    };

    std::array<StateTickOverride, 4> m_stateTickOverrides{
        StateTickOverride(CyberEngineTweaks::AddressHashes::CBaseInitializationState_OnTick, "CBaseInitializationState::OnTick"),
        StateTickOverride(CyberEngineTweaks::AddressHashes::CInitializationState_OnTick, "CInitializationState::OnTick"),
        StateTickOverride(CyberEngineTweaks::AddressHashes::CRunningState_OnTick, "CRunningState::OnTick"),
        StateTickOverride(CyberEngineTweaks::AddressHashes::CShutdownState_OnTick, "CShutdownState::OnTick")};
#endif

    RepeatedTaskQueue m_genericQueue;
};
