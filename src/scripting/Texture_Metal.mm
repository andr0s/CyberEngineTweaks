#include <stdafx.h>

#include "Texture.h"

#include "CET.h"

#include <stb_image.h>

#import <Metal/Metal.h>

std::shared_ptr<Texture> Texture::Load(const std::string& acPath)
{
    auto* pDevice = CET::Get().GetD3D12().GetDevice();
    if (pDevice == nullptr)
        return {};

    // Load from disk into a raw RGBA buffer
    int imageWidth = 0;
    int imageHeight = 0;
    unsigned char* pImageData = stbi_load(acPath.c_str(), &imageWidth, &imageHeight, nullptr, 4);
    if (pImageData == nullptr)
        return {};

    id<MTLDevice> device = (__bridge id<MTLDevice>)pDevice;
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                          width:static_cast<NSUInteger>(imageWidth)
                                                                                         height:static_cast<NSUInteger>(imageHeight)
                                                                                      mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModeShared;

    id<MTLTexture> metalTexture = [device newTextureWithDescriptor:descriptor];
    if (!metalTexture)
    {
        stbi_image_free(pImageData);
        return {};
    }

    [metalTexture replaceRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(imageWidth), static_cast<NSUInteger>(imageHeight))
                    mipmapLevel:0
                      withBytes:pImageData
                    bytesPerRow:static_cast<NSUInteger>(imageWidth) * 4];
    stbi_image_free(pImageData);

    auto texture = std::make_shared<Texture>();
    texture->m_size = ImVec2(static_cast<float>(imageWidth), static_cast<float>(imageHeight));
    texture->m_texture = (void*)CFBridgingRetain(metalTexture);

    return texture;
}

void Texture::ImGuiImage(const Texture& acTexture, ImVec2 aSize, const ImVec2& aUv0, const ImVec2& aUv1, const ImVec4& aTintCol, const ImVec4& aBorderCol)
{
    if (!acTexture.m_texture)
        return;

    if (aSize.x == 0 && aSize.y == 0)
        aSize = acTexture.m_size;

    // the Metal backend takes an id<MTLTexture> as ImTextureID
    ImGui::Image(reinterpret_cast<ImTextureID>(acTexture.m_texture), aSize, aUv0, aUv1, aTintCol, aBorderCol);
}

void Texture::Release()
{
    if (m_texture)
    {
        CFRelease(m_texture);
        m_texture = nullptr;
    }
}

Texture::~Texture()
{
    Release();
}
