#include <stdafx.h>

#include "InputMac.h"

#include "CET.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <CoreGraphics/CoreGraphics.h>
#include <fishhook.h>
#include <mach-o/loader.h>
#include <objc/runtime.h>

namespace
{
bool s_installed = false;

// ---- key codes ---------------------------------------------------------------------------------------------------

// macOS virtual key code (kVK_*) to the Windows virtual-key code CET's bindings use. 0 = not bindable.
USHORT ToWindowsVK(const unsigned short acKeyCode)
{
    switch (acKeyCode)
    {
    case kVK_ANSI_A: return 'A';
    case kVK_ANSI_B: return 'B';
    case kVK_ANSI_C: return 'C';
    case kVK_ANSI_D: return 'D';
    case kVK_ANSI_E: return 'E';
    case kVK_ANSI_F: return 'F';
    case kVK_ANSI_G: return 'G';
    case kVK_ANSI_H: return 'H';
    case kVK_ANSI_I: return 'I';
    case kVK_ANSI_J: return 'J';
    case kVK_ANSI_K: return 'K';
    case kVK_ANSI_L: return 'L';
    case kVK_ANSI_M: return 'M';
    case kVK_ANSI_N: return 'N';
    case kVK_ANSI_O: return 'O';
    case kVK_ANSI_P: return 'P';
    case kVK_ANSI_Q: return 'Q';
    case kVK_ANSI_R: return 'R';
    case kVK_ANSI_S: return 'S';
    case kVK_ANSI_T: return 'T';
    case kVK_ANSI_U: return 'U';
    case kVK_ANSI_V: return 'V';
    case kVK_ANSI_W: return 'W';
    case kVK_ANSI_X: return 'X';
    case kVK_ANSI_Y: return 'Y';
    case kVK_ANSI_Z: return 'Z';
    case kVK_ANSI_0: return '0';
    case kVK_ANSI_1: return '1';
    case kVK_ANSI_2: return '2';
    case kVK_ANSI_3: return '3';
    case kVK_ANSI_4: return '4';
    case kVK_ANSI_5: return '5';
    case kVK_ANSI_6: return '6';
    case kVK_ANSI_7: return '7';
    case kVK_ANSI_8: return '8';
    case kVK_ANSI_9: return '9';
    case kVK_ANSI_Equal: return VK_OEM_PLUS;
    case kVK_ANSI_Minus: return VK_OEM_MINUS;
    case kVK_ANSI_RightBracket: return VK_OEM_6;
    case kVK_ANSI_LeftBracket: return VK_OEM_4;
    case kVK_ANSI_Quote: return VK_OEM_7;
    case kVK_ANSI_Semicolon: return VK_OEM_1;
    case kVK_ANSI_Backslash: return VK_OEM_5;
    case kVK_ANSI_Comma: return VK_OEM_COMMA;
    case kVK_ANSI_Slash: return VK_OEM_2;
    case kVK_ANSI_Period: return VK_OEM_PERIOD;
    case kVK_ANSI_Grave: return VK_OEM_3;
    case kVK_ISO_Section: return VK_OEM_102;
    case kVK_ANSI_KeypadDecimal: return VK_DECIMAL;
    case kVK_ANSI_KeypadMultiply: return VK_MULTIPLY;
    case kVK_ANSI_KeypadPlus: return VK_ADD;
    case kVK_ANSI_KeypadClear: return VK_NUMLOCK; // the key in the Num Lock position
    case kVK_ANSI_KeypadDivide: return VK_DIVIDE;
    case kVK_ANSI_KeypadEnter: return VK_RETURN;
    case kVK_ANSI_KeypadMinus: return VK_SUBTRACT;
    case kVK_ANSI_Keypad0: return VK_NUMPAD0;
    case kVK_ANSI_Keypad1: return VK_NUMPAD1;
    case kVK_ANSI_Keypad2: return VK_NUMPAD2;
    case kVK_ANSI_Keypad3: return VK_NUMPAD3;
    case kVK_ANSI_Keypad4: return VK_NUMPAD4;
    case kVK_ANSI_Keypad5: return VK_NUMPAD5;
    case kVK_ANSI_Keypad6: return VK_NUMPAD6;
    case kVK_ANSI_Keypad7: return VK_NUMPAD7;
    case kVK_ANSI_Keypad8: return VK_NUMPAD8;
    case kVK_ANSI_Keypad9: return VK_NUMPAD9;
    case kVK_Return: return VK_RETURN;
    case kVK_Tab: return VK_TAB;
    case kVK_Space: return VK_SPACE;
    case kVK_Delete: return VK_BACK;
    case kVK_Escape: return VK_ESCAPE;
    case kVK_Command: return VK_LWIN;
    case kVK_RightCommand: return VK_RWIN;
    case kVK_Shift:
    case kVK_RightShift: return VK_SHIFT;
    case kVK_Option:
    case kVK_RightOption: return VK_MENU;
    case kVK_Control:
    case kVK_RightControl: return VK_CONTROL;
    case kVK_CapsLock: return VK_CAPITAL;
    case kVK_F1: return VK_F1;
    case kVK_F2: return VK_F2;
    case kVK_F3: return VK_F3;
    case kVK_F4: return VK_F4;
    case kVK_F5: return VK_F5;
    case kVK_F6: return VK_F6;
    case kVK_F7: return VK_F7;
    case kVK_F8: return VK_F8;
    case kVK_F9: return VK_F9;
    case kVK_F10: return VK_F10;
    case kVK_F11: return VK_F11;
    case kVK_F12: return VK_F12;
    case kVK_F13: return VK_F13;
    case kVK_F14: return VK_F14;
    case kVK_F15: return VK_F15;
    case kVK_F16: return VK_F16;
    case kVK_F17: return VK_F17;
    case kVK_F18: return VK_F18;
    case kVK_F19: return VK_F19;
    case kVK_F20: return VK_F20;
    case kVK_Help: return VK_INSERT; // the key in the Insert position
    case kVK_Home: return VK_HOME;
    case kVK_PageUp: return VK_PRIOR;
    case kVK_ForwardDelete: return VK_DELETE;
    case kVK_End: return VK_END;
    case kVK_PageDown: return VK_NEXT;
    case kVK_LeftArrow: return VK_LEFT;
    case kVK_RightArrow: return VK_RIGHT;
    case kVK_DownArrow: return VK_DOWN;
    case kVK_UpArrow: return VK_UP;
    default: return 0;
    }
}

// For a flagsChanged event: is the modifier whose key changed now down?
bool IsModifierDown(const unsigned short acKeyCode, const NSEventModifierFlags acFlags)
{
    switch (acKeyCode)
    {
    case kVK_Shift:
    case kVK_RightShift: return (acFlags & NSEventModifierFlagShift) != 0;
    case kVK_Control:
    case kVK_RightControl: return (acFlags & NSEventModifierFlagControl) != 0;
    case kVK_Option:
    case kVK_RightOption: return (acFlags & NSEventModifierFlagOption) != 0;
    case kVK_Command:
    case kVK_RightCommand: return (acFlags & NSEventModifierFlagCommand) != 0;
    default: return false;
    }
}

// ---- the game window ---------------------------------------------------------------------------------------------

__weak NSWindow* s_window = nil;

void TrackWindow(NSWindow* apWindow)
{
    if (!apWindow || apWindow == s_window)
        return;

    s_window = apWindow;

    const NSRect frame = apWindow.frame;
    const NSRect content = apWindow.contentView ? apWindow.contentView.bounds : frame;
    CET::Get().GetWindow().SetMacWindow((__bridge HWND)apWindow, {static_cast<LONG>(frame.origin.x), static_cast<LONG>(frame.origin.y)},
                                        {static_cast<LONG>(frame.size.width), static_cast<LONG>(frame.size.height)},
                                        {static_cast<LONG>(content.size.width), static_cast<LONG>(content.size.height)});

    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowDidResignKeyNotification
                                                      object:apWindow
                                                       queue:nil
                                                  usingBlock:^(NSNotification*) {
                                                      // reset key states on focus loss, as the Windows build does on WM_KILLFOCUS
                                                      CET::Get().GetBindings().OnMacFocusLost();
                                                  }];
}

// ---- event filter ------------------------------------------------------------------------------------------------

// The same event can reach both CET's own monitor and a game monitor CET wrapped; handle it once.
__weak NSEvent* s_lastEvent = nil;
bool s_lastConsumed = false;

bool ProcessEvent(NSEvent* apEvent)
{
    if (!s_installed || !apEvent)
        return false;

    if (apEvent == s_lastEvent)
        return s_lastConsumed;

    TrackWindow(apEvent.window);

    auto& bindings = CET::Get().GetBindings();
    switch (apEvent.type)
    {
    case NSEventTypeKeyDown:
        if (!apEvent.isARepeat)
        {
            if (const auto vk = ToWindowsVK(apEvent.keyCode))
                bindings.OnMacKey(vk, true);
        }
        break;
    case NSEventTypeKeyUp:
        if (const auto vk = ToWindowsVK(apEvent.keyCode))
            bindings.OnMacKey(vk, false);
        break;
    case NSEventTypeFlagsChanged:
        if (apEvent.keyCode == kVK_CapsLock)
        {
            bindings.OnMacKey(VK_CAPITAL, true);
            bindings.OnMacKey(VK_CAPITAL, false);
        }
        else if (const auto vk = ToWindowsVK(apEvent.keyCode))
            bindings.OnMacKey(vk, IsModifierDown(apEvent.keyCode, apEvent.modifierFlags));
        break;
    case NSEventTypeLeftMouseDown: bindings.OnMacKey(VK_LBUTTON, true); break;
    case NSEventTypeLeftMouseUp: bindings.OnMacKey(VK_LBUTTON, false); break;
    case NSEventTypeRightMouseDown: bindings.OnMacKey(VK_RBUTTON, true); break;
    case NSEventTypeRightMouseUp: bindings.OnMacKey(VK_RBUTTON, false); break;
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    {
        const bool down = apEvent.type == NSEventTypeOtherMouseDown;
        switch (apEvent.buttonNumber)
        {
        case 2: bindings.OnMacKey(VK_MBUTTON, down); break;
        case 3: bindings.OnMacKey(VK_XBUTTON1, down); break;
        case 4: bindings.OnMacKey(VK_XBUTTON2, down); break;
        default: break;
        }
        break;
    }
    case NSEventTypeScrollWheel:
    {
        // device direction (undo "natural scrolling"), like Windows' wheel delta sign
        const double sign = apEvent.isDirectionInvertedFromDevice ? -1.0 : 1.0;
        const double dy = apEvent.scrollingDeltaY * sign;
        const double dx = apEvent.scrollingDeltaX * sign;
        if (dy != 0.0)
            bindings.OnMacWheel(false, dy > 0.0);
        if (dx != 0.0)
            bindings.OnMacWheel(true, dx > 0.0);
        break;
    }
    default: break;
    }

    s_lastEvent = apEvent;
    s_lastConsumed = CET::Get().GetD3D12().HandleMacEvent((__bridge void*)apEvent);
    return s_lastConsumed;
}

// ---- NSEvent local monitors --------------------------------------------------------------------------------------

using TEventHandler = NSEvent* _Nullable (^)(NSEvent* _Nonnull);
using TAddLocalMonitor = id (*)(id, SEL, NSEventMask, TEventHandler);
TAddLocalMonitor s_realAddLocalMonitor = nullptr;
id s_ownMonitor = nil;

id HookedAddLocalMonitor(id apSelf, SEL apCmd, NSEventMask aMask, TEventHandler aHandler)
{
    if (!aHandler)
        return s_realAddLocalMonitor(apSelf, apCmd, aMask, aHandler);

    TEventHandler wrapped = ^NSEvent* _Nullable(NSEvent* _Nonnull aEvent) {
        if (ProcessEvent(aEvent))
            return nil;
        return aHandler(aEvent);
    };
    return s_realAddLocalMonitor(apSelf, apCmd, aMask, wrapped);
}

// ---- pointer lock --------------------------------------------------------------------------------------------------

using TAssociate = CGError (*)(boolean_t);
using TWarp = CGError (*)(CGPoint);
TAssociate s_realAssociate = nullptr;
TWarp s_realWarp = nullptr;
std::atomic_bool s_overlayCursor{false};
std::atomic_bool s_gameAssociated{true};
bool s_cursorHiddenByOverlay = false;

CGError HookedAssociate(const boolean_t aConnected)
{
    s_gameAssociated = aConnected != 0;
    if (s_overlayCursor)
        return kCGErrorSuccess; // applied when the overlay closes
    return s_realAssociate(aConnected);
}

CGError HookedWarp(const CGPoint aPoint)
{
    if (s_overlayCursor)
        return kCGErrorSuccess;
    return s_realWarp(aPoint);
}

CGError RealAssociate(const boolean_t aConnected)
{
    return s_realAssociate ? s_realAssociate(aConnected) : CGAssociateMouseAndMouseCursorPosition(aConnected);
}
} // namespace

void InputMac::InstallEarlyHooks()
{
    if (s_installed)
        return;

    @autoreleasepool
    {
        // Wrap the monitors the game installs from now on.
        Method addMonitor = class_getClassMethod([NSEvent class], @selector(addLocalMonitorForEventsMatchingMask:handler:));
        if (addMonitor)
        {
            s_realAddLocalMonitor = reinterpret_cast<TAddLocalMonitor>(method_setImplementation(addMonitor, reinterpret_cast<IMP>(&HookedAddLocalMonitor)));
            Log::Info("NSEvent local monitor hook complete!");
        }
        else
            Log::Error("Could not hook NSEvent local monitors!");

        // CET's own monitor sees events that reach the game through its views' responder methods.
        if (s_realAddLocalMonitor)
        {
            s_ownMonitor = s_realAddLocalMonitor([NSEvent class], @selector(addLocalMonitorForEventsMatchingMask:handler:), NSEventMaskAny, ^NSEvent* _Nullable(NSEvent* _Nonnull aEvent) {
                return ProcessEvent(aEvent) ? nil : aEvent;
            });
        }
    }

    // The game's own calls only (its import table), so other code in the process is not affected.
    const auto* header = Platform::GetGameImageHeader();
    rebinding rebinds[] = {
        {"CGAssociateMouseAndMouseCursorPosition", reinterpret_cast<void*>(&HookedAssociate), reinterpret_cast<void**>(&s_realAssociate)},
        {"CGWarpMouseCursorPosition", reinterpret_cast<void*>(&HookedWarp), reinterpret_cast<void**>(&s_realWarp)},
    };
    if (!header || rebind_symbols_image(const_cast<mach_header_64*>(header), Platform::GetGameImageSlide(), rebinds, std::size(rebinds)) != 0 || !s_realAssociate ||
        !s_realWarp)
        Log::Warn("Could not route the game's pointer lock through CET; the pointer may stay locked while the overlay is open.");
    else
        Log::Info("Pointer lock hooks complete!");

    s_installed = true;
}

void InputMac::SetOverlayCursor(const bool aOverlayOwnsCursor)
{
    if (aOverlayOwnsCursor == s_overlayCursor)
        return;

    if (aOverlayOwnsCursor)
    {
        s_overlayCursor = true;
        RealAssociate(true);
        if (!s_cursorHiddenByOverlay)
        {
            // ImGui draws the pointer while the overlay is open; NSCursor hide/unhide calls are counted, so this pair
            // leaves whatever the game set unchanged
            [NSCursor hide];
            s_cursorHiddenByOverlay = true;
        }
    }
    else
    {
        if (s_cursorHiddenByOverlay)
        {
            [NSCursor unhide];
            s_cursorHiddenByOverlay = false;
        }
        s_overlayCursor = false;
        RealAssociate(s_gameAssociated ? 1 : 0);
    }
}
