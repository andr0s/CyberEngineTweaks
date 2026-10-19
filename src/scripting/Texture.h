#pragma once

struct Texture
{
    static void BindTexture(sol::table& aTable);
    static std::shared_ptr<Texture> Load(const std::string& acPath);
    static void ImGuiImage(
        const Texture& acTexture, ImVec2 aSize = ImVec2(0, 0), const ImVec2& aUv0 = ImVec2(0, 0), const ImVec2& aUv1 = ImVec2(1, 1), const ImVec4& aTintCol = ImVec4(1, 1, 1, 1),
        const ImVec4& aBorderCol = ImVec4(0, 0, 0, 0));

    ImVec2 GetSize() const;

    void Release();

#ifdef __APPLE__
    ~Texture();
#else
    ~Texture() = default;
#endif

private:
#ifdef __APPLE__
    void* m_texture{nullptr}; // id<MTLTexture>, retained
#else
    Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
    D3D12_GPU_DESCRIPTOR_HANDLE m_handle;
#endif
    ImVec2 m_size{};
};
