#include <stdafx.h>

#include "window.h"

// On macOS CET does not subclass the game window: input arrives through the event monitor in
// platform/macos/InputMac.mm, which also reports the window here once the game has created it.

Window::Window(VKBindings* apBindings, D3D12* apD3D12)
    : m_pBindings(apBindings)
    , m_pD3D12(apD3D12)
{
}

Window::~Window() = default;

void Window::SetMacWindow(HWND apWindow, POINT aPos, SIZE aSize, SIZE aClientSize)
{
    m_hWnd = apWindow;
    m_wndPos = aPos;
    m_wndSize = aSize;
    m_clientPos = {0, 0};
    m_clientSize = aClientSize;
    m_initialized = apWindow != nullptr;
}
