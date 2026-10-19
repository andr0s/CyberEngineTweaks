#pragma once

// Minimal Windows names used by CET's platform-neutral code, for the macOS build only.
// RED4ext/Detail/WinCompat.hpp (included through the SDK) already defines HMODULE, HANDLE, BOOL, DWORD, WORD,
// BYTE, UINT, WCHAR, MAX_PATH and __declspec. Everything that touches the OS is implemented separately for macOS.

#ifndef __APPLE__
#error "platform/macos/WinCompat.h is for the macOS build only"
#endif

#include <cstdint>
#include <cstring>
#include <strings.h>

#ifdef __OBJC__
// objc.h defines BOOL as bool; keep RED4ext's Windows BOOL (int) out of its way in Objective-C++ files. This header
// must be the first to include RED4ext/Detail/WinCompat.hpp (stdafx.h includes it first).
#import <objc/objc.h>
#define BOOL RED4extWinBOOL
#include <RED4ext/Detail/WinCompat.hpp>
#undef BOOL
#else
#include <RED4ext/Detail/WinCompat.hpp>
#endif

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _byteswap_ushort __builtin_bswap16
#define _byteswap_ulong __builtin_bswap32
#define _byteswap_uint64 __builtin_bswap64

using USHORT = uint16_t;
using LONG = int32_t;
using LRESULT = intptr_t;
using WPARAM = uintptr_t;
using LPARAM = intptr_t;
using HWND = void*;

struct POINT
{
    LONG x;
    LONG y;
};

struct SIZE
{
    LONG cx;
    LONG cy;
};

#ifndef _CRT_WIDE
#define _CRT_WIDE_(s) L##s
#define _CRT_WIDE(s) _CRT_WIDE_(s)
#endif

// Raw input wheel flags. CET only uses them to build its own wheel pseudo key codes (VKBC_MWHEEL*).
#define RI_MOUSE_WHEEL 0x0400
#define RI_MOUSE_HWHEEL 0x0800

// Windows virtual-key codes. CET stores bindings as these codes (bindings.json), so the macOS input layer translates
// macOS key codes into them and saved bindings stay compatible between platforms.
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_F16 0x7F
#define VK_F17 0x80
#define VK_F18 0x81
#define VK_F19 0x82
#define VK_F20 0x83
#define VK_F21 0x84
#define VK_F22 0x85
#define VK_F23 0x86
#define VK_F24 0x87
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA      // ;:
#define VK_OEM_PLUS 0xBB   // =+
#define VK_OEM_COMMA 0xBC  // ,<
#define VK_OEM_MINUS 0xBD  // -_
#define VK_OEM_PERIOD 0xBE // .>
#define VK_OEM_2 0xBF      // /?
#define VK_OEM_3 0xC0      // `~
#define VK_OEM_4 0xDB      // [{
#define VK_OEM_5 0xDC      // \|
#define VK_OEM_6 0xDD      // ]}
#define VK_OEM_7 0xDE      // '"
#define VK_OEM_102 0xE2    // the extra key on ISO keyboards

#define MAPVK_VK_TO_CHAR 2

// MapVirtualKey(code, MAPVK_VK_TO_CHAR) for the US layout: the unshifted character of a printable key, else 0.
// CET uses it only to print key names in its bindings UI.
inline UINT MapVirtualKey(UINT aCode, UINT aMapType)
{
    if (aMapType != MAPVK_VK_TO_CHAR)
        return 0;
    if ((aCode >= '0' && aCode <= '9') || (aCode >= 'A' && aCode <= 'Z'))
        return aCode;
    switch (aCode)
    {
    case VK_OEM_1: return ';';
    case VK_OEM_PLUS: return '=';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_2: return '/';
    case VK_OEM_3: return '`';
    case VK_OEM_4: return '[';
    case VK_OEM_5: return '\\';
    case VK_OEM_6: return ']';
    case VK_OEM_7: return '\'';
    case VK_OEM_102: return '\\';
    case VK_MULTIPLY: return '*';
    case VK_ADD: return '+';
    case VK_SUBTRACT: return '-';
    case VK_DECIMAL: return '.';
    case VK_DIVIDE: return '/';
    default: return 0;
    }
}
