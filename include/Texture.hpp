#pragma once
#include "Common.hpp"

class Texture
{
public:
    void LoadPpm(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                 const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
    ID3D12Resource* Resource() const { return m_resource.Get(); }

private:
    ComPtr<ID3D12Resource> m_resource;
    ComPtr<ID3D12Resource> m_upload;
};

