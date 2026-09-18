#pragma once
#include "Common.hpp"

class GBuffer
{
public:
    //базовый цвет, нормаль и мировая позиция.
    static constexpr UINT TargetCount = 3;
    void Initialize(ID3D12Device* device, UINT width, UINT height,
                    D3D12_CPU_DESCRIPTOR_HANDLE srvStart, UINT srvIncrement);
    void BeginGeometry(ID3D12GraphicsCommandList* commandList);
    void EndGeometry(ID3D12GraphicsCommandList* commandList);

    D3D12_CPU_DESCRIPTOR_HANDLE RtvStart() const { return m_rtvHeap->GetCPUDescriptorHandleForHeapStart(); }
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const { return m_dsvHeap->GetCPUDescriptorHandleForHeapStart(); }
    const std::array<DXGI_FORMAT, TargetCount>& Formats() const { return m_formats; }

private:
    std::array<ComPtr<ID3D12Resource>, TargetCount> m_targets; // Текстуры 
    ComPtr<ID3D12Resource> m_depth; // Общая глубина геом. прохода
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap; // Для записи г-буффера
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap; // Отдельно для глубины
    UINT m_rtvIncrement = 0;
    std::array<DXGI_FORMAT, TargetCount> m_formats{
        DXGI_FORMAT_R8G8B8A8_UNORM,       // Albedo
        DXGI_FORMAT_R16G16B16A16_FLOAT,   // Normal
        DXGI_FORMAT_R16G16B16A16_FLOAT    // World position
    };
};
