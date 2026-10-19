#pragma once

// Input for CET on macOS, scoped to the game's own events (the replacement for the Windows WNDPROC subclass).
//
// The game reads keyboard and mouse through NSEvent: its view's responder methods and an NSEvent local monitor (the
// 2.3.1 binary uses addLocalMonitorForEventsMatchingMask:handler:, keyDown:/keyUp:/flagsChanged:/mouseMoved: and the
// events' deltaX/deltaY for mouse look). CET installs its own local monitor and wraps the monitors the game adds, so
// it sees each event first: bindings observe every event, and while the overlay has the input, the event is dropped
// before the game gets it. Local monitors only receive this process's events; nothing system-wide is captured and no
// Accessibility permission is involved.
//
// While the overlay is open the game must not lock or warp the pointer, so the game's calls to
// CGAssociateMouseAndMouseCursorPosition and CGWarpMouseCursorPosition (through its import table) are routed through
// CET, which keeps the pointer free until the overlay closes and then restores what the game asked for. The overlay
// draws its own pointer (ImGui software cursor) and hides the system one meanwhile.

namespace InputMac
{
// At plugin load, after CET started and before the game creates its window and monitors.
void InstallEarlyHooks();

// Overlay open (true) or closed (false); main thread.
void SetOverlayCursor(bool aOverlayOwnsCursor);
} // namespace InputMac
