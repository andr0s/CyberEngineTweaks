#pragma once

#ifdef _WIN32
#include "common/D3D12Downlevel.h"
#endif
#include "window/window.h"

#ifdef _WIN32
using TCRenderNode_Present_InternalPresent = void*(int32_t*, uint8_t, UINT);
using TCRenderGlobal_Resize = void*(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t a4, int32_t* a5);
using TCRenderGlobal_Shutdown = void*(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
#endif

// On macOS this class renders CET's ImGui overlay with Metal (d3d12/D3D12_Metal.mm); the name is kept so the rest of
// CET is unchanged.
struct D3D12
{
#ifdef _WIN32
    inline static const uint32_t g_numDownlevelBackbuffersRequired = 3; // Windows 7 only: number of buffers needed before we start rendering
#endif

    D3D12(Window& aWindow, Paths& aPaths, Options& aOptions);
    ~D3D12();

    void ReloadFonts();

    void SetTrapInputInImGui(const bool acEnabled);
    void DelayedSetTrapInputInImGui(const bool acEnabled);
    [[nodiscard]] bool IsTrapInputInImGui() const noexcept { return m_trapInputInImGui; }
    [[nodiscard]] bool IsInitialized() const noexcept { return m_initialized; }
    [[nodiscard]] SIZE GetResolution() const noexcept { return m_outSize; }

#ifdef __APPLE__
    // An NSEvent from the game window, on the main thread (platform/macos/InputMac.mm). Returns true when the
    // overlay consumed it and the game must not see it.
    bool HandleMacEvent(void* apEvent);

    // Presentation of the game's drawable (render thread): CET draws its overlay into it first.
    void OnPresent(void* apCommandBuffer, void* apDrawable);

    // id<MTLDevice> of the game, once the first frame was presented.
    [[nodiscard]] void* GetDevice() const;
#else
    LRESULT OnWndProc(HWND ahWnd, UINT auMsg, WPARAM awParam, LPARAM alParam) const;
#endif

    TiltedPhoques::Signal<void()> OnInitialized;

#ifdef _WIN32
    ID3D12Device* GetDevice() const;
    std::tuple<D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE> CreateTextureDescriptor();
#endif

protected:
    void Hook();

#ifdef _WIN32
    struct FrameContext
    {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> CommandList{};
        Microsoft::WRL::ComPtr<ID3D12Resource> BackBuffer;
        D3D12_CPU_DESCRIPTOR_HANDLE MainRenderTargetDescriptor{0};
    };
#endif

    bool ResetState(const bool acDestroyContext = false);
#ifdef __APPLE__
    bool Initialize(void* apDrawable);
    bool InitializeImGui();
    void NewPlatformFrame();
    void Update(void* apCommandBuffer, void* apDrawable);
#else
    bool Initialize();
    bool InitializeImGui(size_t aBuffersCounts);
    void Update();
#endif

    void PrepareUpdate();

#ifdef _WIN32
    static void* CRenderNode_Present_InternalPresent(int32_t* apSomeInt, uint8_t aSomeSync, UINT aSyncInterval);
    static void* CRenderGlobal_Resize(uint32_t a1, uint32_t a2, uint32_t a3, uint8_t a4, int32_t* a5);
    static void* CRenderGlobal_Shutdown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);
#endif

private:
#ifdef _WIN32
    TCRenderNode_Present_InternalPresent* m_realInternalPresent{nullptr};
    TCRenderGlobal_Resize* m_realInternalResize{nullptr};
    TCRenderGlobal_Shutdown* m_realInternalShutdown{nullptr};
#endif

    bool m_initialized{false};

#ifdef __APPLE__
    void* m_device{nullptr};                                   // id<MTLDevice>, retained
    std::atomic_bool m_resetRequested{false};                  // the drawable size changed
    std::chrono::steady_clock::time_point m_lastFrameTime{};
#else
    TiltedPhoques::Vector<FrameContext> m_frameContexts;

    Microsoft::WRL::ComPtr<ID3D12Device> m_pd3d12Device{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pd3dRtvDescHeap{};
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_pd3dSrvDescHeap{};

    // borrowed resources from game, do not manipulate reference counts on these!
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_pdxgiSwapChain{nullptr};
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_pCommandQueue{nullptr};
#endif

    SIZE m_outSize{};

    std::atomic_bool m_trapInputInImGui{false};
    ImGuiStyle m_styleReference{};

    Paths& m_paths;
    Window& m_window;
    Options& m_options;

    std::recursive_mutex m_imguiLock;
    std::array<ImDrawData, 3> m_imguiDrawDataBuffers;
    std::atomic_bool m_delayedTrapInput{false};
    std::atomic_bool m_delayedTrapInputState{false};
};
