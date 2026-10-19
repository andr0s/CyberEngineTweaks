#pragma once

struct D3D12;
struct VKBindings;

struct Window
{
    Window(VKBindings* apBindings, D3D12* apD3D12);
    ~Window();

    HWND GetWindow() const { return m_hWnd; }

    POINT GetWndPos() const { return m_wndPos; }
    SIZE GetWndSize() const { return m_wndSize; }

    POINT GetClientPos() const { return m_clientPos; }
    SIZE GetClientSize() const { return m_clientSize; }

    bool IsInitialized() const { return m_initialized; }

#ifdef __APPLE__
    // The game's NSWindow, reported by the input layer once the game has created it, and its frame in points.
    void SetMacWindow(HWND apWindow, POINT aPos, SIZE aSize, SIZE aClientSize);
#else
protected:
    static LRESULT APIENTRY WndProc(HWND ahWnd, UINT auMsg, WPARAM awParam, LPARAM alParam);
#endif

private:
    bool m_initialized{false};

    HWND m_hWnd{nullptr};
#ifndef __APPLE__
    WNDPROC m_wndProc{nullptr};
#endif

    POINT m_wndPos{};
    SIZE m_wndSize{};
    POINT m_clientPos{};
    SIZE m_clientSize{};

    VKBindings* m_pBindings;
    D3D12* m_pD3D12;
};
