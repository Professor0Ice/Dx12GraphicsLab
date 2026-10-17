#pragma once
#include "Common.hpp"

class Texture
{
public:
    void LoadPng(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, bool heightMap = false);
    void LoadPpm(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

    void LoadDds(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);
    ID3D12Resource* Resource() const { return m_resource.Get(); }

private:
    void Upload2D(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT width, UINT height, DXGI_FORMAT format, const uint8_t* pixels, UINT rowPitch, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle);

    ComPtr<ID3D12Resource> m_resource;
    ComPtr<ID3D12Resource> m_upload;
};
