#pragma once
#include "Common.hpp"

class Texture
{
public:
    void LoadPpm(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                 const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
    // Loads a precomputed uncompressed RGBA8 DDS texture. Both regular 2D maps and
    // cubemaps (including their complete mip chains) are supported.
    void LoadDds(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                 const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
    ID3D12Resource* Resource() const { return m_resource.Get(); }

private:
    ComPtr<ID3D12Resource> m_resource; // Конечная техтура для видюхи
    ComPtr<ID3D12Resource> m_upload; // Промежуточный буфер для процессора
};
