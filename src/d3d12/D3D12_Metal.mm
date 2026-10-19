// macOS implementation of CET's overlay renderer (the Windows one is D3D12.cpp, D3D12_Hooks.cpp and the DX parts of
// D3D12_Functions.cpp). Same structure as on Windows: the ImGui frame is built on the game's main thread
// (PrepareUpdate, a GameMainThread task) and the last finished frame is drawn into the game's drawable when the game
// presents it.
//
// The game presents through -[MTLCommandBuffer presentDrawable:...] (2.3.1 uses the afterMinimumDuration: variant for
// frame pacing). At that point the game may still be encoding into the command buffer, so CET only remembers the
// drawable there and adds its render pass when the command buffer is committed, after all of the game's encoders have
// ended. The concrete command buffer class is found by creating one on the game's device; only that class is swizzled.

#include <stdafx.h>

#include "D3D12.h"
#include "CET.h"

#include <imgui_impl/macos_keys.h>
#include <imgui_impl/metal.h>
#include <platform/macos/InputMac.h>
#include <scripting/GameHooks.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <objc/runtime.h>

namespace
{
char s_pendingDrawableKey;
char s_drawnKey;

id<MTLCommandQueue> s_ownQueue = nil; // for drawables the game presents without a command buffer

using TPresent = void (*)(id, SEL, id);
using TPresentTime = void (*)(id, SEL, id, CFTimeInterval);
using TCommit = void (*)(id, SEL);
using TDrawablePresent = void (*)(id, SEL);
using TDrawablePresentTime = void (*)(id, SEL, CFTimeInterval);

TPresent s_realPresent = nullptr;
TPresentTime s_realPresentAtTime = nullptr;
TPresentTime s_realPresentAfter = nullptr;
TCommit s_realCommit = nullptr;
TDrawablePresent s_realDrawablePresent = nullptr;
TDrawablePresentTime s_realDrawablePresentAtTime = nullptr;
TDrawablePresentTime s_realDrawablePresentAfter = nullptr;

// Replaces aSelector on aClass only: when the class inherits the method, it gets its own copy first, so superclasses
// (shared with other classes) are left alone.
IMP Swizzle(Class aClass, SEL aSelector, IMP aReplacement)
{
    if (!aClass)
        return nullptr;

    Method method = class_getInstanceMethod(aClass, aSelector);
    if (!method)
        return nullptr;

    if (class_addMethod(aClass, aSelector, method_getImplementation(method), method_getTypeEncoding(method)))
        method = class_getInstanceMethod(aClass, aSelector);

    return method_setImplementation(method, aReplacement);
}

bool IsCETRunning()
{
    return CET::IsRunning();
}

void RememberDrawable(id aCommandBuffer, id aDrawable)
{
    if (aDrawable && IsCETRunning())
        objc_setAssociatedObject(aCommandBuffer, &s_pendingDrawableKey, aDrawable, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

void HookedPresent(id aSelf, SEL aCmd, id aDrawable)
{
    RememberDrawable(aSelf, aDrawable);
    s_realPresent(aSelf, aCmd, aDrawable);
}

void HookedPresentAtTime(id aSelf, SEL aCmd, id aDrawable, CFTimeInterval aTime)
{
    RememberDrawable(aSelf, aDrawable);
    s_realPresentAtTime(aSelf, aCmd, aDrawable, aTime);
}

void HookedPresentAfter(id aSelf, SEL aCmd, id aDrawable, CFTimeInterval aDuration)
{
    RememberDrawable(aSelf, aDrawable);
    s_realPresentAfter(aSelf, aCmd, aDrawable, aDuration);
}

void HookedCommit(id aSelf, SEL aCmd)
{
    if (id drawable = objc_getAssociatedObject(aSelf, &s_pendingDrawableKey))
    {
        objc_setAssociatedObject(aSelf, &s_pendingDrawableKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        objc_setAssociatedObject(drawable, &s_drawnKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        if (IsCETRunning())
            CET::Get().GetD3D12().OnPresent((__bridge void*)aSelf, (__bridge void*)drawable);
    }

    s_realCommit(aSelf, aCmd);
}

// A drawable presented directly (not through a command buffer) gets the overlay from CET's own queue.
void DrawBeforeDirectPresent(id aDrawable)
{
    if (!IsCETRunning() || objc_getAssociatedObject(aDrawable, &s_drawnKey))
        return;

    objc_setAssociatedObject(aDrawable, &s_drawnKey, @YES, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    if (!s_ownQueue)
    {
        id<MTLDevice> device = [(id<CAMetalDrawable>)aDrawable texture].device;
        s_ownQueue = [device newCommandQueue];
        s_ownQueue.label = @"CET overlay";
    }

    id<MTLCommandBuffer> commandBuffer = [s_ownQueue commandBuffer];
    CET::Get().GetD3D12().OnPresent((__bridge void*)commandBuffer, (__bridge void*)aDrawable);
    s_realCommit(commandBuffer, @selector(commit));
}

void HookedDrawablePresent(id aSelf, SEL aCmd)
{
    DrawBeforeDirectPresent(aSelf);
    s_realDrawablePresent(aSelf, aCmd);
}

void HookedDrawablePresentAtTime(id aSelf, SEL aCmd, CFTimeInterval aTime)
{
    DrawBeforeDirectPresent(aSelf);
    s_realDrawablePresentAtTime(aSelf, aCmd, aTime);
}

void HookedDrawablePresentAfter(id aSelf, SEL aCmd, CFTimeInterval aDuration)
{
    DrawBeforeDirectPresent(aSelf);
    s_realDrawablePresentAfter(aSelf, aCmd, aDuration);
}

bool IsLinearTarget(id<CAMetalDrawable> aDrawable)
{
    switch (aDrawable.texture.pixelFormat)
    {
    case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatBGRA10_XR_sRGB:
    case MTLPixelFormatBGR10_XR_sRGB: return true;
    default: break;
    }

    CGColorSpaceRef colorspace = aDrawable.layer.colorspace;
    if (!colorspace)
        return false;

    CFStringRef name = CGColorSpaceGetName(colorspace);
    return name && CFStringFind(name, CFSTR("Linear"), 0).location != kCFNotFound;
}
} // namespace

D3D12::D3D12(Window& aWindow, Paths& aPaths, Options& aOptions)
    : m_paths(aPaths)
    , m_window(aWindow)
    , m_options(aOptions)
{
    Hook();

    // add repeated task which prepares next ImGui frame for update
    GameMainThread::Get().AddGenericTask(
        [this]
        {
            PrepareUpdate();
            return false;
        });
}

D3D12::~D3D12()
{
    assert(!m_initialized);
}

void D3D12::Hook()
{
    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        Class commandBufferClass = commandBuffer ? object_getClass(commandBuffer) : nil;
        if (!commandBufferClass)
        {
            Log::Error("D3D12::Hook() - no Metal device; the overlay is disabled.");
            return;
        }

        s_realCommit = reinterpret_cast<TCommit>(Swizzle(commandBufferClass, @selector(commit), reinterpret_cast<IMP>(&HookedCommit)));
        s_realPresent = reinterpret_cast<TPresent>(Swizzle(commandBufferClass, @selector(presentDrawable:), reinterpret_cast<IMP>(&HookedPresent)));
        s_realPresentAtTime = reinterpret_cast<TPresentTime>(Swizzle(commandBufferClass, @selector(presentDrawable:atTime:), reinterpret_cast<IMP>(&HookedPresentAtTime)));
        s_realPresentAfter =
            reinterpret_cast<TPresentTime>(Swizzle(commandBufferClass, @selector(presentDrawable:afterMinimumDuration:), reinterpret_cast<IMP>(&HookedPresentAfter)));

        if (!s_realCommit || !s_realPresent)
        {
            Log::Error("D3D12::Hook() - could not hook {}; the overlay is disabled.", class_getName(commandBufferClass));
            return;
        }

        Class drawableClass = objc_getClass("CAMetalDrawable");
        s_realDrawablePresent = reinterpret_cast<TDrawablePresent>(Swizzle(drawableClass, @selector(present), reinterpret_cast<IMP>(&HookedDrawablePresent)));
        s_realDrawablePresentAtTime =
            reinterpret_cast<TDrawablePresentTime>(Swizzle(drawableClass, @selector(presentAtTime:), reinterpret_cast<IMP>(&HookedDrawablePresentAtTime)));
        s_realDrawablePresentAfter =
            reinterpret_cast<TDrawablePresentTime>(Swizzle(drawableClass, @selector(presentAfterMinimumDuration:), reinterpret_cast<IMP>(&HookedDrawablePresentAfter)));

        Log::Info("D3D12::Hook() - Metal present hooks complete ({}).", class_getName(commandBufferClass));
    }
}

void* D3D12::GetDevice() const
{
    return m_device;
}

void D3D12::SetTrapInputInImGui(const bool acEnabled)
{
    InputMac::SetOverlayCursor(acEnabled);

    {
        std::lock_guard _(m_imguiLock);
        if (ImGui::GetCurrentContext())
        {
            auto& io = ImGui::GetIO();
            io.MouseDrawCursor = acEnabled;
            if (!acEnabled)
                io.ClearInputKeys();
        }
    }

    m_trapInputInImGui = acEnabled;
}

void D3D12::DelayedSetTrapInputInImGui(const bool acEnabled)
{
    m_delayedTrapInputState = acEnabled;
    m_delayedTrapInput = true;
}

bool D3D12::HandleMacEvent(void* apEvent)
{
    if (m_delayedTrapInput)
    {
        SetTrapInputInImGui(m_delayedTrapInputState);
        m_delayedTrapInput = false;
    }

    if (!m_initialized || !m_trapInputInImGui)
        return false;

    NSEvent* event = (__bridge NSEvent*)apEvent;

    std::lock_guard _(m_imguiLock);
    if (!ImGui::GetCurrentContext())
        return false;

    auto& io = ImGui::GetIO();

    const auto addModifiers = [&io](NSEventModifierFlags aFlags)
    {
        io.AddKeyEvent(ImGuiMod_Ctrl, (aFlags & NSEventModifierFlagControl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (aFlags & NSEventModifierFlagShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (aFlags & NSEventModifierFlagOption) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (aFlags & NSEventModifierFlagCommand) != 0);
    };

    const auto addMousePosition = [this, &io, event]
    {
        NSView* view = event.window.contentView;
        if (!view)
            return;

        const NSPoint point = [view convertPoint:event.locationInWindow fromView:nil];
        const NSSize bounds = view.bounds.size;
        if (bounds.width <= 0 || bounds.height <= 0)
            return;

        // ImGui works in the drawable's pixels
        const auto scaleX = static_cast<float>(m_outSize.cx) / static_cast<float>(bounds.width);
        const auto scaleY = static_cast<float>(m_outSize.cy) / static_cast<float>(bounds.height);
        const auto y = view.isFlipped ? point.y : bounds.height - point.y;
        io.AddMousePosEvent(static_cast<float>(point.x) * scaleX, static_cast<float>(y) * scaleY);
    };

    switch (event.type)
    {
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged: addMousePosition(); return true;

    case NSEventTypeLeftMouseDown:
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseDown:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseDown:
    case NSEventTypeOtherMouseUp:
    {
        addMousePosition();
        const bool down = event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeOtherMouseDown;
        const auto button = static_cast<int>(event.buttonNumber);
        if (button >= 0 && button < ImGuiMouseButton_COUNT)
            io.AddMouseButtonEvent(button, down);
        return true;
    }

    case NSEventTypeScrollWheel:
    {
        // same scaling as imgui_impl_osx
        double dx = event.scrollingDeltaX;
        double dy = event.scrollingDeltaY;
        if (event.hasPreciseScrollingDeltas)
        {
            dx *= 0.01;
            dy *= 0.01;
        }
        else
        {
            dx *= 0.1;
            dy *= 0.1;
        }
        if (dx != 0.0 || dy != 0.0)
            io.AddMouseWheelEvent(static_cast<float>(dx), static_cast<float>(dy));
        return true;
    }

    case NSEventTypeKeyDown:
    case NSEventTypeKeyUp:
    {
        const bool down = event.type == NSEventTypeKeyDown;
        addModifiers(event.modifierFlags);
        if (const auto key = ImGui_ImplOSX_KeyCodeToImGuiKey(event.keyCode); key != ImGuiKey_None)
        {
            io.AddKeyEvent(key, down);
            io.SetKeyEventNativeData(key, event.keyCode, -1);
        }

        // text, except for shortcuts and the function-key characters AppKit uses (U+F700..U+F8FF)
        if (down && (event.modifierFlags & NSEventModifierFlagCommand) == 0)
        {
            NSString* characters = event.characters;
            for (NSUInteger i = 0; i < characters.length; ++i)
            {
                const unichar c = [characters characterAtIndex:i];
                if (c >= 0x20 && c != 0x7F && !(c >= 0xF700 && c <= 0xF8FF))
                    io.AddInputCharacterUTF16(c);
            }
        }
        return true;
    }

    case NSEventTypeFlagsChanged:
    {
        addModifiers(event.modifierFlags);
        if (const auto key = ImGui_ImplOSX_KeyCodeToImGuiKey(event.keyCode); key != ImGuiKey_None)
        {
            bool down = false;
            switch (event.keyCode)
            {
            case kVK_Shift:
            case kVK_RightShift: down = (event.modifierFlags & NSEventModifierFlagShift) != 0; break;
            case kVK_Control:
            case kVK_RightControl: down = (event.modifierFlags & NSEventModifierFlagControl) != 0; break;
            case kVK_Option:
            case kVK_RightOption: down = (event.modifierFlags & NSEventModifierFlagOption) != 0; break;
            case kVK_Command:
            case kVK_RightCommand: down = (event.modifierFlags & NSEventModifierFlagCommand) != 0; break;
            case kVK_CapsLock: down = (event.modifierFlags & NSEventModifierFlagCapsLock) != 0; break;
            default: break;
            }
            io.AddKeyEvent(key, down);
        }
        return true;
    }

    default: return false;
    }
}

bool D3D12::ResetState(const bool acDestroyContext)
{
    std::lock_guard _(m_imguiLock);

    if (m_initialized)
    {
        for (auto& drawData : m_imguiDrawDataBuffers)
        {
            for (auto i = 0; i < drawData.CmdListsCount; ++i)
                IM_DELETE(drawData.CmdLists[i]);
            drawData.Clear();
        }

        ImGui_ImplMetal_Shutdown();

        if (acDestroyContext)
            ImGui::DestroyContext();
    }

    if (m_device)
    {
        CFRelease(m_device);
        m_device = nullptr;
    }

    m_outSize = {0, 0};
    m_initialized = false;

    return false;
}

bool D3D12::Initialize(void* apDrawable)
{
    if (m_initialized)
        return true;

    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)apDrawable;
    id<MTLTexture> texture = drawable.texture;
    if (!texture)
        return false;

    std::lock_guard _(m_imguiLock);

    m_device = (void*)CFBridgingRetain(texture.device);
    m_outSize = {static_cast<LONG>(texture.width), static_cast<LONG>(texture.height)};

    const bool linear = IsLinearTarget(drawable);
    ImGui_ImplMetal_SetLinearOutput(linear);

    if (!InitializeImGui())
    {
        Log::Error("D3D12::Initialize() - failed to initialize ImGui!");
        return ResetState();
    }

    Log::Info("D3D12::Initialize() - initialization successful! Target {}x{}, pixel format {}, {} output.", m_outSize.cx, m_outSize.cy, static_cast<int>(texture.pixelFormat),
              linear ? "linear" : "sRGB");
    m_initialized = true;

    OnInitialized.Emit();

    return true;
}

bool D3D12::InitializeImGui()
{
    std::lock_guard _(m_imguiLock);

    // TODO - scale also by DPI
    const auto [resx, resy] = m_outSize;
    const auto scaleFromReference = std::min(static_cast<float>(resx) / 1920.0f, static_cast<float>(resy) / 1080.0f);

    if (ImGui::GetCurrentContext() == nullptr)
    {
        // do this once, do not repeat context creation!
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        // TODO - make this configurable eventually and overridable by mods for themselves easily
        // setup CET default style
        ImGui::StyleColorsDark(&m_styleReference);
        m_styleReference.WindowRounding = 6.0f;
        m_styleReference.WindowTitleAlign.x = 0.5f;
        m_styleReference.ChildRounding = 6.0f;
        m_styleReference.PopupRounding = 6.0f;
        m_styleReference.FrameRounding = 6.0f;
        m_styleReference.ScrollbarRounding = 12.0f;
        m_styleReference.GrabRounding = 12.0f;
        m_styleReference.TabRounding = 6.0f;
    }

    ImGui::GetStyle() = m_styleReference;
    ImGui::GetStyle().ScaleAllSizes(scaleFromReference);

    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigMacOSXBehaviors = true;
    io.BackendPlatformName = "cet_macos";
    io.MouseDrawCursor = m_trapInputInImGui;

    if (!ImGui_ImplMetal_Init((__bridge id<MTLDevice>)m_device))
    {
        Log::Error("D3D12::InitializeImGui() - ImGui_ImplMetal_Init call failed!");
        return false;
    }

    ReloadFonts();

    if (!ImGui_ImplMetal_CreateDeviceObjects((__bridge id<MTLDevice>)m_device))
    {
        Log::Error("D3D12::InitializeImGui() - ImGui_ImplMetal_CreateDeviceObjects call failed!");
        ImGui_ImplMetal_Shutdown();
        return false;
    }

    m_lastFrameTime = std::chrono::steady_clock::now();
    return true;
}

void D3D12::NewPlatformFrame()
{
    // main thread, m_imguiLock held (PrepareUpdate)
    if (m_delayedTrapInput)
    {
        SetTrapInputInImGui(m_delayedTrapInputState);
        m_delayedTrapInput = false;
    }

    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(m_outSize.cx), static_cast<float>(m_outSize.cy));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration<float>(now - m_lastFrameTime).count();
    io.DeltaTime = delta > 0.0f ? delta : 1.0f / 60.0f;
    m_lastFrameTime = now;

    io.MouseDrawCursor = m_trapInputInImGui;
}

void D3D12::OnPresent(void* apCommandBuffer, void* apDrawable)
{
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)apDrawable;
    id<MTLTexture> texture = drawable.texture;
    if (!texture)
        return;

    if (m_initialized && (static_cast<LONG>(texture.width) != m_outSize.cx || static_cast<LONG>(texture.height) != m_outSize.cy))
    {
        Log::Info("D3D12::OnPresent() - drawable size changed to {}x{}, resetting the overlay.", texture.width, texture.height);
        ResetState();
    }

    if (!m_initialized)
    {
        Initialize(apDrawable);
        return;
    }

    Update(apCommandBuffer, apDrawable);
}

void D3D12::Update(void* apCommandBuffer, void* apDrawable)
{
    // swap staging ImGui buffer with render ImGui buffer
    {
        std::lock_guard _(m_imguiLock);
        if (m_imguiDrawDataBuffers[1].Valid)
        {
            std::swap(m_imguiDrawDataBuffers[0], m_imguiDrawDataBuffers[1]);
            m_imguiDrawDataBuffers[1].Valid = false;
        }
    }

    if (!m_imguiDrawDataBuffers[0].Valid)
        return;

    id<MTLCommandBuffer> commandBuffer = (__bridge id<MTLCommandBuffer>)apCommandBuffer;
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)apDrawable;

    MTLRenderPassDescriptor* renderPass = [MTLRenderPassDescriptor renderPassDescriptor];
    renderPass.colorAttachments[0].texture = drawable.texture;
    renderPass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    renderPass.colorAttachments[0].storeAction = MTLStoreActionStore;

    {
        std::lock_guard _(m_imguiLock);
        ImGui_ImplMetal_NewFrame(renderPass);
    }

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:renderPass];
    if (!encoder)
        return;

    encoder.label = @"Cyber Engine Tweaks";
    ImGui_ImplMetal_RenderDrawData(&m_imguiDrawDataBuffers[0], commandBuffer, encoder);
    [encoder endEncoding];
}
