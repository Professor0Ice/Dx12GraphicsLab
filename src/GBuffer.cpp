#include "GBuffer.hpp"

void GBuffer::Initialize(ID3D12Device* device, UINT width, UINT height,
                         D3D12_CPU_DESCRIPTOR_HANDLE srvStart, UINT srvIncrement)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = TargetCount;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)), "Create GBuffer RTV heap");
    m_rtvIncrement = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = 1;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)), "Create DSV heap");

    const auto heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srv = srvStart;
    for (UINT i = 0; i < TargetCount; ++i)
    {
        D3D12_CLEAR_VALUE clear{};
        clear.Format = m_formats[i];
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = m_formats[i];
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&m_targets[i])), "Create GBuffer target");
        device->CreateRenderTargetView(m_targets[i].Get(), nullptr, rtv);
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = m_formats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(m_targets[i].Get(), &srvDesc, srv);
        rtv.ptr += m_rtvIncrement;
        srv.ptr += srvIncrement;
    }

    D3D12_CLEAR_VALUE depthClear{};
    depthClear.Format = DXGI_FORMAT_D32_FLOAT;
    depthClear.DepthStencil.Depth = 1.0f;
    D3D12_RESOURCE_DESC depthDesc{};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&m_depth)), "Create depth buffer");
    device->CreateDepthStencilView(m_depth.Get(), nullptr, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void GBuffer::BeginGeometry(ID3D12GraphicsCommandList* commandList)
{
    std::array<D3D12_RESOURCE_BARRIER, TargetCount> barriers{};
    for (UINT i = 0; i < TargetCount; ++i)
        barriers[i] = TransitionBarrier(m_targets[i].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET);
    commandList->ResourceBarrier(TargetCount, barriers.data());

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, TargetCount> rtvs{};
    rtvs[0] = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 1; i < TargetCount; ++i)
    {
        rtvs[i] = rtvs[i - 1];
        rtvs[i].ptr += m_rtvIncrement;
    }
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = Dsv();
    commandList->OMSetRenderTargets(TargetCount, rtvs.data(), FALSE, &dsv);
    const float clearAlbedo[4]{ 0.02f, 0.025f, 0.035f, 1.0f };
    const float clearZero[4]{};
    commandList->ClearRenderTargetView(rtvs[0], clearAlbedo, 0, nullptr);
    commandList->ClearRenderTargetView(rtvs[1], clearZero, 0, nullptr);
    commandList->ClearRenderTargetView(rtvs[2], clearZero, 0, nullptr);
    commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void GBuffer::EndGeometry(ID3D12GraphicsCommandList* commandList)
{
    std::array<D3D12_RESOURCE_BARRIER, TargetCount> barriers{};
    for (UINT i = 0; i < TargetCount; ++i)
        barriers[i] = TransitionBarrier(m_targets[i].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(TargetCount, barriers.data());
}
