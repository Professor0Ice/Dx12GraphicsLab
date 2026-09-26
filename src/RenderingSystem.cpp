#include "RenderingSystem.hpp"

using namespace DirectX;

namespace
{
    constexpr XMFLOAT3 SunDirection{ -0.25f, -1.0f, 0.20f };
    constexpr XMFLOAT4 SunColorAndIntensity{ 1.0f, 0.92f, 0.78f, 1.35f };
    constexpr float DirectionalLightType = 0.0f;
    constexpr UINT SunLightIndex = 0;

    D3D12_RASTERIZER_DESC DefaultRasterizer()
    {
        D3D12_RASTERIZER_DESC result{};
        result.FillMode = D3D12_FILL_MODE_SOLID;// Без каркаса
        result.CullMode = D3D12_CULL_MODE_BACK; // Не рисовать обратные стороны полигонов
        result.FrontCounterClockwise = FALSE;// Вершины лицевой стороны идут по часовой стрелке
        result.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
        result.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
        result.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
        result.DepthClipEnable = TRUE; 
        result.MultisampleEnable = FALSE;
        result.AntialiasedLineEnable = FALSE;
        result.ForcedSampleCount = 0;
        result.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
        return result;
    }

    D3D12_BLEND_DESC DefaultBlend()
    {
        D3D12_BLEND_DESC result{};
        result.AlphaToCoverageEnable = FALSE;
        result.IndependentBlendEnable = FALSE;
        const D3D12_RENDER_TARGET_BLEND_DESC target{
            FALSE, FALSE,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
            D3D12_LOGIC_OP_NOOP, D3D12_COLOR_WRITE_ENABLE_ALL
        };
        for (auto& item : result.RenderTarget) item = target; 
        return result;
    }

    D3D12_DEPTH_STENCIL_DESC DefaultDepth()
    {
        D3D12_DEPTH_STENCIL_DESC result{};
        result.DepthEnable = TRUE;
        result.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        result.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        result.StencilEnable = FALSE;
        return result;
    }

    D3D12_STATIC_SAMPLER_DESC AnisotropicSampler()
    {
        // Правила чтения текстур
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_ANISOTROPIC; 
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP; 
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxAnisotropy = 8;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MinLOD = 0;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0; //register(s0).
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        return sampler;
    }

    D3D12_STATIC_SAMPLER_DESC ShadowComparisonSampler()
    {
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL; 
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = 0.0f;
        sampler.ShaderRegister = 1; //register(s1).
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return sampler;
    }

    D3D12_STATIC_SAMPLER_DESC IblSampler()
    {
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 2; // register(s2).
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return sampler;
    }
}

RenderingSystem::RenderingSystem(HWND window, UINT width, UINT height)
    : m_width(width), m_height(height), m_runtimeDirectory(ExecutableDirectory())
{
    //Размеры окна
    m_viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    m_scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) }; 
    CreateDeviceAndSwapChain(window);
    CreateDescriptors();
    CreateRootSignatures();
    CreatePipelineStates();
    CreateConstantUpload();
    CreateTessellationCache();
    CreateParticleResources();
    LoadAssets();
    BuildScene();

    //Отпрка массива команд в видюху
    ThrowIfFailed(m_commandList->Close(), "Close initialization command list");
    ID3D12CommandList* lists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists); 
    FlushGpu(); 
}

RenderingSystem::~RenderingSystem()
{
    if (m_commandQueue and m_fence)
        FlushGpu();
    if (m_constantUpload and m_constantMapped)
        m_constantUpload->Unmap(0, nullptr);
    if (m_fenceEvent)
        CloseHandle(m_fenceEvent);
}

void RenderingSystem::CreateDeviceAndSwapChain(HWND window)
{
    // Настройка директа
    UINT factoryFlags = 0;
#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
    {
        debug->EnableDebugLayer(); 
        factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif
    ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)), "Create DXGI factory");

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT index = 0; m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&adapter)) not_eq DXGI_ERROR_NOT_FOUND; ++index)
    {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description); 
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; } 
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
        adapter.Reset();
    }
    if (not m_device)
    {
        ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Find WARP adapter");
        ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),"Create D3D12 device");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "Create command queue");
    for (auto& allocator : m_allocators)
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),"Create command allocator");
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr,IID_PPV_ARGS(&m_commandList)), "Create command list");

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = m_width;
    swapDesc.Height = m_height;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // 8-битный формат цвета
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // В буферы разрешено рисовать
    swapDesc.BufferCount = FrameCount;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Современная быстрая модель показа кадров
    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), window, &swapDesc, nullptr, nullptr,&swapChain), "Create swap chain");
    ThrowIfFailed(swapChain.As(&m_swapChain), "Query swap chain 3");

    // Fence — монотонный счётчик завершённых GPU-команд; событие позволяет ждать его без busy loop.
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Create fence");
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (not m_fenceEvent) throw std::runtime_error("CreateEventW failed");
}

void RenderingSystem::CreateDescriptors()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.NumDescriptors = FrameCount;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_swapChainRtvHeap)),
                  "Create swap chain RTV heap");
    m_rtvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); 
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_swapChainRtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < FrameCount; ++i)
    {
        ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])), "Get swap chain buffer"); 
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv); 
        rtv.ptr += m_rtvIncrement;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
    srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDesc.NumDescriptors = 27;
    srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&m_srvHeap)), "Create SRV heap");
    m_srvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_gbuffer.Initialize(m_device.Get(), m_width, m_height, CpuSrv(0), m_srvIncrement);
    CreateShadowResources();
}

void RenderingSystem::CreateShadowResources()
{
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.NumDescriptors = CascadeCount;
    ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_shadowDsvHeap)),
                  "Create shadow DSV heap");
    const UINT dsvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    D3D12_CLEAR_VALUE clear{};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = ShadowMapSize;
    desc.Height = ShadowMapSize;
    desc.DepthOrArraySize = CascadeCount; 
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    const auto heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&m_shadowMap)),"Create cascaded shadow map");

    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = cascade;
        dsvDesc.Texture2DArray.ArraySize = 1;
        m_device->CreateDepthStencilView(m_shadowMap.Get(), &dsvDesc, dsv);
        dsv.ptr += dsvIncrement;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT; 
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = CascadeCount;
    m_device->CreateShaderResourceView(m_shadowMap.Get(), &srvDesc, CpuSrv(3));

    m_shadowViewport = { 0.0f, 0.0f, static_cast<float>(ShadowMapSize),static_cast<float>(ShadowMapSize), 0.0f, 1.0f };
    m_shadowScissor = { 0, 0, static_cast<LONG>(ShadowMapSize), static_cast<LONG>(ShadowMapSize) };
}

void RenderingSystem::CreateRootSignatures()
{
    D3D12_DESCRIPTOR_RANGE geometryRange{};
    geometryRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    geometryRange.NumDescriptors = 3;
    geometryRange.BaseShaderRegister = 0; 
    geometryRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> geometryParams{};
    geometryParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; 
    geometryParams[0].Descriptor.ShaderRegister = 0;
    geometryParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    geometryParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; 
    geometryParams[1].DescriptorTable.NumDescriptorRanges = 1;
    geometryParams[1].DescriptorTable.pDescriptorRanges = &geometryRange;
    geometryParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    auto sampler = AnisotropicSampler();
    D3D12_ROOT_SIGNATURE_DESC geometryDesc{};
    geometryDesc.NumParameters = static_cast<UINT>(geometryParams.size());
    geometryDesc.pParameters = geometryParams.data();
    geometryDesc.NumStaticSamplers = 1;
    geometryDesc.pStaticSamplers = &sampler;
    geometryDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                         D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT;

    ComPtr<ID3DBlob> signature, errors;
    HRESULT hr = D3D12SerializeRootSignature(&geometryDesc, D3D_ROOT_SIGNATURE_VERSION_1,&signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),IID_PPV_ARGS(&m_geometryRootSignature)), "Create geometry root signature");

    // t0-t3 находятся в начале heap, а заранее рассчитанные IBL-карты лежат в
    // слотах 16-18, не пересекаясь с таблицами материалов и частиц.
    std::array<D3D12_DESCRIPTOR_RANGE, 2> lightingRanges{};
    lightingRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingRanges[0].NumDescriptors = GBuffer::TargetCount + 1;
    lightingRanges[0].BaseShaderRegister = 0;
    lightingRanges[0].OffsetInDescriptorsFromTableStart = 0;
    lightingRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingRanges[1].NumDescriptors = 3;
    lightingRanges[1].BaseShaderRegister = 4;
    lightingRanges[1].OffsetInDescriptorsFromTableStart = 16;
    std::array<D3D12_ROOT_PARAMETER, 2> lightingParams{};
    lightingParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lightingParams[0].Descriptor.ShaderRegister = 0;
    lightingParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    lightingParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[1].DescriptorTable.NumDescriptorRanges = static_cast<UINT>(lightingRanges.size());
    lightingParams[1].DescriptorTable.pDescriptorRanges = lightingRanges.data();
    lightingParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC lightingDesc{};
    lightingDesc.NumParameters = static_cast<UINT>(lightingParams.size());
    lightingDesc.pParameters = lightingParams.data();
    const std::array<D3D12_STATIC_SAMPLER_DESC, 3> lightingSamplers{
        sampler, ShadowComparisonSampler(), IblSampler()
    };
    lightingDesc.NumStaticSamplers = static_cast<UINT>(lightingSamplers.size());
    lightingDesc.pStaticSamplers = lightingSamplers.data();
    hr = D3D12SerializeRootSignature(&lightingDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),IID_PPV_ARGS(&m_lightingRootSignature)), "Create lighting root signature");

    D3D12_ROOT_PARAMETER shadowParameter{};
    shadowParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    shadowParameter.Descriptor.ShaderRegister = 0;
    shadowParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC shadowDesc{};
    shadowDesc.NumParameters = 1;
    shadowDesc.pParameters = &shadowParameter;
    shadowDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                       D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    hr = D3D12SerializeRootSignature(&shadowDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                IID_PPV_ARGS(&m_shadowRootSignature)), "Create shadow root signature");

    D3D12_DESCRIPTOR_RANGE particleUavRange{};
    particleUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    particleUavRange.NumDescriptors = 2;
    particleUavRange.BaseShaderRegister = 0;
    particleUavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> particleComputeParams{};
    particleComputeParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    particleComputeParams[0].Descriptor.ShaderRegister = 0;
    particleComputeParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    particleComputeParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    particleComputeParams[1].DescriptorTable.NumDescriptorRanges = 1;
    particleComputeParams[1].DescriptorTable.pDescriptorRanges = &particleUavRange;
    particleComputeParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC particleComputeDesc{};
    particleComputeDesc.NumParameters = static_cast<UINT>(particleComputeParams.size());
    particleComputeDesc.pParameters = particleComputeParams.data();
    particleComputeDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                                D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    hr = D3D12SerializeRootSignature(&particleComputeDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "Particle compute root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_particleComputeRootSignature)), "Create particle compute root signature");

    D3D12_DESCRIPTOR_RANGE particleSrvRange{};
    particleSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    particleSrvRange.NumDescriptors = 1;
    particleSrvRange.BaseShaderRegister = 0;
    particleSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> particleDrawParams{};
    particleDrawParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    particleDrawParams[0].Descriptor.ShaderRegister = 0;
    particleDrawParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_GEOMETRY;
    particleDrawParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    particleDrawParams[1].DescriptorTable.NumDescriptorRanges = 1;
    particleDrawParams[1].DescriptorTable.pDescriptorRanges = &particleSrvRange;
    particleDrawParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC particleDrawDesc{};
    particleDrawDesc.NumParameters = static_cast<UINT>(particleDrawParams.size());
    particleDrawDesc.pParameters = particleDrawParams.data();
    particleDrawDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                             D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                             D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                             D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
    hr = D3D12SerializeRootSignature(&particleDrawDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "Particle draw root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_particleDrawRootSignature)), "Create particle draw root signature");

    D3D12_DESCRIPTOR_RANGE sortSrvRange{};
    sortSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sortSrvRange.NumDescriptors = 1;
    sortSrvRange.BaseShaderRegister = 0;
    D3D12_DESCRIPTOR_RANGE sortUavRange{};
    sortUavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    sortUavRange.NumDescriptors = 1;
    sortUavRange.BaseShaderRegister = 0;
    std::array<D3D12_ROOT_PARAMETER, 3> sortParams{};
    sortParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    sortParams[0].Descriptor.ShaderRegister = 0;
    sortParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sortParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    sortParams[1].DescriptorTable.NumDescriptorRanges = 1;
    sortParams[1].DescriptorTable.pDescriptorRanges = &sortSrvRange;
    sortParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    sortParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    sortParams[2].DescriptorTable.NumDescriptorRanges = 1;
    sortParams[2].DescriptorTable.pDescriptorRanges = &sortUavRange;
    sortParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC sortDesc{};
    sortDesc.NumParameters = static_cast<UINT>(sortParams.size());
    sortDesc.pParameters = sortParams.data();
    hr = D3D12SerializeRootSignature(&sortDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "Particle sort root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_particleSortRootSignature)), "Create particle sort root signature");

    D3D12_DESCRIPTOR_RANGE sortedIndicesRange{};
    sortedIndicesRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    sortedIndicesRange.NumDescriptors = 1;
    sortedIndicesRange.BaseShaderRegister = 1;
    std::array<D3D12_ROOT_PARAMETER, 3> transparentDrawParams{};
    transparentDrawParams[0] = particleDrawParams[0];
    transparentDrawParams[1] = particleDrawParams[1];
    transparentDrawParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    transparentDrawParams[2].DescriptorTable.NumDescriptorRanges = 1;
    transparentDrawParams[2].DescriptorTable.pDescriptorRanges = &sortedIndicesRange;
    transparentDrawParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    D3D12_ROOT_SIGNATURE_DESC transparentDrawDesc = particleDrawDesc;
    transparentDrawDesc.NumParameters = static_cast<UINT>(transparentDrawParams.size());
    transparentDrawDesc.pParameters = transparentDrawParams.data();
    hr = D3D12SerializeRootSignature(&transparentDrawDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                     &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "Transparent particle draw root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_transparentParticleDrawRootSignature)),
        "Create transparent particle draw root signature");
}

ComPtr<ID3DBlob> RenderingSystem::CompileShader(const std::filesystem::path& file,const char* entry, const char* target) const
{
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS; 
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> shader, errors;
    HRESULT hr = D3DCompileFromFile(file.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                    entry, target, flags, 0, &shader, &errors);
    if (FAILED(hr))
    {
        std::string message = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Shader compilation failed";
        message += "\nFile: " + file.string();
        throw std::runtime_error(message);
    }
    return shader;
}

void RenderingSystem::CreatePipelineStates()
{
    const auto geometryVs = CompileShader(m_runtimeDirectory / L"shader/Geometry.hlsl", "VSMain", "vs_5_1");
    const auto geometryPs = CompileShader(m_runtimeDirectory / L"shader/Geometry.hlsl", "PSMain", "ps_5_1");
    const auto lightingVs = CompileShader(m_runtimeDirectory / L"shader/FullscreenQuad.hlsl", "VSMain", "vs_5_1");
    const auto lightingPs = CompileShader(m_runtimeDirectory / L"shader/PostProcess.hlsl", "PSMain", "ps_5_1");
    const auto tessVs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "VSMain", "vs_5_1");
    const auto tessHs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "HSMain", "hs_5_1");
    const auto tessDs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "DSMain", "ds_5_1");
    const auto tessPs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "PSMain", "ps_5_1");
    const auto tessCacheGs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "GSCache", "gs_5_1");
    const auto cachedTessVs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "CachedVS", "vs_5_1");
    const auto shadowVs = CompileShader(m_runtimeDirectory / L"shader/Shadow.hlsl", "VSMain", "vs_5_1");
    const auto cachedShadowVs = CompileShader(m_runtimeDirectory / L"shader/ShadowTessellation.hlsl", "CachedVS", "vs_5_1");
    const auto particleInitializeCs = CompileShader(m_runtimeDirectory / L"shader/ParticleUpdate.hlsl", "CSInitialize", "cs_5_1");
    const auto particleCs = CompileShader(m_runtimeDirectory / L"shader/ParticleUpdate.hlsl", "CSMain", "cs_5_1");
    const auto reverseParticleInitializeCs = CompileShader(m_runtimeDirectory / L"shader/ParticleUpdate.hlsl", "CSInitializeReverse", "cs_5_1");
    const auto reverseParticleCs = CompileShader(m_runtimeDirectory / L"shader/ParticleUpdate.hlsl", "CSReverse", "cs_5_1");
    const auto particleSortBuildCs = CompileShader(m_runtimeDirectory / L"shader/ParticleSort.hlsl", "CSBuildKeys", "cs_5_1");
    const auto particleSortStepCs = CompileShader(m_runtimeDirectory / L"shader/ParticleSort.hlsl", "CSBitonicStep", "cs_5_1");
    const auto particleVs = CompileShader(m_runtimeDirectory / L"shader/ParticleRender.hlsl", "VSMain", "vs_5_1");
    const auto particleSortedVs = CompileShader(m_runtimeDirectory / L"shader/ParticleRender.hlsl", "VSSorted", "vs_5_1");
    const auto particleGs = CompileShader(m_runtimeDirectory / L"shader/ParticleRender.hlsl", "GSMain", "gs_5_1");
    const auto particlePs = CompileShader(m_runtimeDirectory / L"shader/ParticleRender.hlsl", "PSMain", "ps_5_1");
    const auto transparentParticlePs = CompileShader(m_runtimeDirectory / L"shader/ParticleRender.hlsl", "PSTransparent", "ps_5_1");

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    const D3D12_INPUT_ELEMENT_DESC cachedLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometry{};
    geometry.pRootSignature = m_geometryRootSignature.Get(); 
    geometry.VS = { geometryVs->GetBufferPointer(), geometryVs->GetBufferSize() }; // Байт-код vertex shader.
    geometry.PS = { geometryPs->GetBufferPointer(), geometryPs->GetBufferSize() }; // Байт-код pixel shader.
    geometry.BlendState = DefaultBlend();
    geometry.SampleMask = UINT_MAX;
    geometry.RasterizerState = DefaultRasterizer();
    geometry.DepthStencilState = DefaultDepth();
    geometry.InputLayout = { inputLayout, static_cast<UINT>(std::size(inputLayout)) };
    geometry.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; // PSO ожидает треугольники.
    geometry.NumRenderTargets = GBuffer::TargetCount; 
    for (UINT i = 0; i < GBuffer::TargetCount; ++i) geometry.RTVFormats[i] = m_gbuffer.Formats()[i];
    geometry.DSVFormat = DXGI_FORMAT_D32_FLOAT; 
    geometry.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&geometry, IID_PPV_ARGS(&m_geometryPso)),"Create geometry PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC sprite = geometry;
    sprite.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&sprite, IID_PPV_ARGS(&m_spritePso)),
                  "Create sprite PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessellation = geometry;
    tessellation.VS = { tessVs->GetBufferPointer(), tessVs->GetBufferSize() };
    tessellation.HS = { tessHs->GetBufferPointer(), tessHs->GetBufferSize() };
    tessellation.DS = { tessDs->GetBufferPointer(), tessDs->GetBufferSize() };
    tessellation.PS = { tessPs->GetBufferPointer(), tessPs->GetBufferSize() };
    tessellation.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    tessellation.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; 
    const D3D12_SO_DECLARATION_ENTRY streamLayout[] = {
        { 0, "POSITION", 0, 0, 3, 0 },
        { 0, "NORMAL",   0, 0, 3, 0 },
        { 0, "TANGENT",  0, 0, 3, 0 },
        { 0, "TEXCOORD", 0, 0, 2, 0 }
    };
    const UINT streamStride = CachedVertexStride;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC capture = tessellation;
    capture.GS = { tessCacheGs->GetBufferPointer(), tessCacheGs->GetBufferSize() };
    capture.PS = {};
    capture.NumRenderTargets = 0;
    for (auto& format : capture.RTVFormats) format = DXGI_FORMAT_UNKNOWN;
    capture.DSVFormat = DXGI_FORMAT_UNKNOWN;
    capture.DepthStencilState.DepthEnable = FALSE;
    capture.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    capture.StreamOutput = { streamLayout, static_cast<UINT>(std::size(streamLayout)),
                             &streamStride, 1, D3D12_SO_NO_RASTERIZED_STREAM };
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&capture, IID_PPV_ARGS(&m_tessCapturePso)),
                  "Create tessellation stream output PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC cachedTessellation = geometry;
    cachedTessellation.VS = { cachedTessVs->GetBufferPointer(), cachedTessVs->GetBufferSize() };
    cachedTessellation.PS = { tessPs->GetBufferPointer(), tessPs->GetBufferSize() };
    cachedTessellation.InputLayout = { cachedLayout, static_cast<UINT>(std::size(cachedLayout)) };
    cachedTessellation.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&cachedTessellation,
        IID_PPV_ARGS(&m_cachedTessellationPso)), "Create cached tessellation PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lighting{};
    lighting.pRootSignature = m_lightingRootSignature.Get();
    lighting.VS = { lightingVs->GetBufferPointer(), lightingVs->GetBufferSize() };
    lighting.PS = { lightingPs->GetBufferPointer(), lightingPs->GetBufferSize() };
    lighting.BlendState = DefaultBlend();
    lighting.SampleMask = UINT_MAX;
    lighting.RasterizerState = DefaultRasterizer();
    lighting.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    lighting.DepthStencilState = DefaultDepth();
    lighting.DepthStencilState.DepthEnable = FALSE; 
    lighting.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    lighting.InputLayout = { nullptr, 0 }; 
    lighting.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lighting.NumRenderTargets = 1;
    lighting.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    lighting.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&lighting, IID_PPV_ARGS(&m_lightingPso)),"Create lighting PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow{};
    shadow.pRootSignature = m_shadowRootSignature.Get();
    shadow.VS = { shadowVs->GetBufferPointer(), shadowVs->GetBufferSize() };
    shadow.BlendState = DefaultBlend();
    shadow.SampleMask = UINT_MAX;
    shadow.RasterizerState = DefaultRasterizer();
    shadow.RasterizerState.DepthBias = 1200; 
    shadow.RasterizerState.DepthBiasClamp = 0.01f; 
    shadow.RasterizerState.SlopeScaledDepthBias = 1.5f; 
    shadow.DepthStencilState = DefaultDepth();
    shadow.InputLayout = { inputLayout, static_cast<UINT>(std::size(inputLayout)) };
    shadow.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadow.NumRenderTargets = 0; 
    shadow.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadow.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&m_shadowPso)),"Create shadow PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC spriteShadow = shadow;
    spriteShadow.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&spriteShadow,
        IID_PPV_ARGS(&m_spriteShadowPso)), "Create sprite shadow PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC cachedShadow = shadow;
    cachedShadow.pRootSignature = m_geometryRootSignature.Get();
    cachedShadow.VS = { cachedShadowVs->GetBufferPointer(), cachedShadowVs->GetBufferSize() };
    cachedShadow.InputLayout = { cachedLayout, static_cast<UINT>(std::size(cachedLayout)) };
    cachedShadow.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&cachedShadow,
        IID_PPV_ARGS(&m_cachedShadowPso)), "Create cached tessellation shadow PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleInitialize{};
    particleInitialize.pRootSignature = m_particleComputeRootSignature.Get();
    particleInitialize.CS = { particleInitializeCs->GetBufferPointer(), particleInitializeCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&particleInitialize,
        IID_PPV_ARGS(&m_particleInitializePso)), "Create particle initialization PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleCompute{};
    particleCompute.pRootSignature = m_particleComputeRootSignature.Get();
    particleCompute.CS = { particleCs->GetBufferPointer(), particleCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&particleCompute,
        IID_PPV_ARGS(&m_particleComputePso)), "Create particle compute PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC reverseParticleInitialize = particleInitialize;
    reverseParticleInitialize.CS = {
        reverseParticleInitializeCs->GetBufferPointer(), reverseParticleInitializeCs->GetBufferSize()
    };
    ThrowIfFailed(m_device->CreateComputePipelineState(&reverseParticleInitialize,
        IID_PPV_ARGS(&m_reverseParticleInitializePso)), "Create reverse particle initialization PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC reverseParticleCompute = particleCompute;
    reverseParticleCompute.CS = { reverseParticleCs->GetBufferPointer(), reverseParticleCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&reverseParticleCompute,
        IID_PPV_ARGS(&m_reverseParticleComputePso)), "Create reverse particle compute PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleSortBuild{};
    particleSortBuild.pRootSignature = m_particleSortRootSignature.Get();
    particleSortBuild.CS = { particleSortBuildCs->GetBufferPointer(), particleSortBuildCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&particleSortBuild,
        IID_PPV_ARGS(&m_particleSortBuildPso)), "Create particle sort-key PSO");

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleSortStep = particleSortBuild;
    particleSortStep.CS = { particleSortStepCs->GetBufferPointer(), particleSortStepCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&particleSortStep,
        IID_PPV_ARGS(&m_particleSortStepPso)), "Create particle bitonic-sort PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC particleDraw{};
    particleDraw.pRootSignature = m_particleDrawRootSignature.Get();
    particleDraw.VS = { particleVs->GetBufferPointer(), particleVs->GetBufferSize() };
    particleDraw.GS = { particleGs->GetBufferPointer(), particleGs->GetBufferSize() };
    particleDraw.PS = { particlePs->GetBufferPointer(), particlePs->GetBufferSize() };
    particleDraw.BlendState = DefaultBlend();
    particleDraw.SampleMask = UINT_MAX;
    particleDraw.RasterizerState = DefaultRasterizer();
    particleDraw.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    particleDraw.DepthStencilState = DefaultDepth();
    particleDraw.InputLayout = { nullptr, 0 };
    particleDraw.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    particleDraw.NumRenderTargets = GBuffer::TargetCount;
    for (UINT i = 0; i < GBuffer::TargetCount; ++i) particleDraw.RTVFormats[i] = m_gbuffer.Formats()[i];
    particleDraw.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    particleDraw.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&particleDraw,
        IID_PPV_ARGS(&m_particleDrawPso)), "Create particle draw PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentParticleDraw = particleDraw;
    transparentParticleDraw.pRootSignature = m_transparentParticleDrawRootSignature.Get();
    transparentParticleDraw.VS = { particleSortedVs->GetBufferPointer(), particleSortedVs->GetBufferSize() };
    transparentParticleDraw.PS = {
        transparentParticlePs->GetBufferPointer(), transparentParticlePs->GetBufferSize()
    };
    transparentParticleDraw.BlendState.RenderTarget[0].BlendEnable = TRUE;
    transparentParticleDraw.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparentParticleDraw.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparentParticleDraw.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    transparentParticleDraw.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    transparentParticleDraw.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    transparentParticleDraw.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparentParticleDraw.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    transparentParticleDraw.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    transparentParticleDraw.NumRenderTargets = 1;
    transparentParticleDraw.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    transparentParticleDraw.RTVFormats[1] = DXGI_FORMAT_UNKNOWN;
    transparentParticleDraw.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&transparentParticleDraw,
        IID_PPV_ARGS(&m_transparentParticleDrawPso)), "Create transparent particle draw PSO");
}

void RenderingSystem::CreateConstantUpload()
{
    const auto heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    auto desc = BufferDescription(ConstantsPerFrame * FrameCount);
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_constantUpload)), "Create constant upload buffer");
    D3D12_RANGE readRange{ 0, 0 }; 
    ThrowIfFailed(m_constantUpload->Map(0, &readRange, reinterpret_cast<void**>(&m_constantMapped)),
                  "Map constant upload buffer");
}

void RenderingSystem::CreateTessellationCache()
{
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    auto verticesDesc = BufferDescription(CachedVertexBufferBytes);
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,&verticesDesc, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr,IID_PPV_ARGS(&m_cachedTessVertices)), "Create tessellation vertex cache");
    m_cachedTessVertexView = { m_cachedTessVertices->GetGPUVirtualAddress(),static_cast<UINT>(CachedVertexBufferBytes), CachedVertexStride };

    auto filledSizeDesc = BufferDescription(sizeof(UINT));
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,&filledSizeDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_cachedTessFilledSize)), "Create stream output counter");

    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE,&filledSizeDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,IID_PPV_ARGS(&m_tessFilledSizeReset)), "Create stream output counter reset");
    void* resetValue = nullptr;
    const D3D12_RANGE noCpuReads{ 0, 0 };
    ThrowIfFailed(m_tessFilledSizeReset->Map(0, &noCpuReads, &resetValue), "Map stream output counter reset");
    *static_cast<UINT*>(resetValue) = 0;
    const D3D12_RANGE writtenBytes{ 0, sizeof(UINT) };
    m_tessFilledSizeReset->Unmap(0, &writtenBytes);

    auto drawArgsDesc = BufferDescription(sizeof(D3D12_DRAW_ARGUMENTS));
    drawArgsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE,&drawArgsDesc, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, nullptr,IID_PPV_ARGS(&m_cachedTessDrawArgs)), "Create cached tessellation draw arguments");

    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    parameters[0].Descriptor.ShaderRegister = 0;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameters[1].Descriptor.ShaderRegister = 0;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = parameters;
    ComPtr<ID3DBlob> signature, errors;
    const HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                     &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer())
                                        : "Tessellation draw args root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(),
        signature->GetBufferSize(), IID_PPV_ARGS(&m_tessDrawArgsRootSignature)),
        "Create tessellation draw args root signature");

    const auto drawArgsCs = CompileShader(m_runtimeDirectory / L"shader/TessellationDrawArgs.hlsl",
                                          "CSMain", "cs_5_1");
    D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
    compute.pRootSignature = m_tessDrawArgsRootSignature.Get();
    compute.CS = { drawArgsCs->GetBufferPointer(), drawArgsCs->GetBufferSize() };
    ThrowIfFailed(m_device->CreateComputePipelineState(&compute, IID_PPV_ARGS(&m_tessDrawArgsPso)),
                  "Create tessellation draw args PSO");

    D3D12_INDIRECT_ARGUMENT_DESC argument{};
    argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
    D3D12_COMMAND_SIGNATURE_DESC commandSignature{};
    commandSignature.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
    commandSignature.NumArgumentDescs = 1;
    commandSignature.pArgumentDescs = &argument;
    ThrowIfFailed(m_device->CreateCommandSignature(&commandSignature, nullptr,
        IID_PPV_ARGS(&m_tessDrawCommandSignature)), "Create cached tessellation draw signature");
}

void RenderingSystem::CreateParticleResources()
{
    static_assert(sizeof(ParticleGpu) == 32, "Particle layout must match HLSL");
    static_assert(sizeof(ParticleSortItem) == 8, "Particle sort layout must match HLSL");
    constexpr UINT64 particleBytes = static_cast<UINT64>(MaxParticles) * sizeof(ParticleGpu);
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);

    auto particleDesc = BufferDescription(particleBytes);
    particleDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_particleBuffers[0])),"Create first particle buffer");
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_particleBuffers[1])),"Create second particle buffer");
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_reverseParticleBuffers[0])),"Create first reverse particle buffer");
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &particleDesc,D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_reverseParticleBuffers[1])),"Create second reverse particle buffer");

    auto sortItemsDesc = BufferDescription(static_cast<UINT64>(MaxParticles) * sizeof(ParticleSortItem));
    sortItemsDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &sortItemsDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&m_reverseParticleSortItems)),
        "Create reverse particle sort buffer");

    auto counterDesc = BufferDescription(sizeof(UINT));
    counterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    for (auto& counter : m_particleCounters)
        ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &counterDesc,D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&counter)),"Create particle UAV counter");
    for (auto& counter : m_reverseParticleCounters)
        ThrowIfFailed(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &counterDesc,D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&counter)),"Create reverse particle UAV counter");

    auto counterUploadDesc = BufferDescription(sizeof(UINT));
    ThrowIfFailed(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &counterUploadDesc,D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_particleCounterUpload)),"Create particle counter upload buffer");
    void* mappedCounters = nullptr;
    const D3D12_RANGE noCpuReads{ 0, 0 };
    ThrowIfFailed(m_particleCounterUpload->Map(0, &noCpuReads, &mappedCounters), "Map particle counter upload");
    *static_cast<UINT*>(mappedCounters) = 0;
    const D3D12_RANGE counterWriteRange{ 0, sizeof(UINT) };
    m_particleCounterUpload->Unmap(0, &counterWriteRange);
    m_commandList->CopyBufferRegion(m_particleCounters[0].Get(), 0,m_particleCounterUpload.Get(), 0, sizeof(UINT));
    m_commandList->CopyBufferRegion(m_particleCounters[1].Get(), 0,m_particleCounterUpload.Get(), 0, sizeof(UINT));
    m_commandList->CopyBufferRegion(m_reverseParticleCounters[0].Get(), 0,m_particleCounterUpload.Get(), 0, sizeof(UINT));
    m_commandList->CopyBufferRegion(m_reverseParticleCounters[1].Get(), 0,m_particleCounterUpload.Get(), 0, sizeof(UINT));
    std::array<D3D12_RESOURCE_BARRIER, 4> countersToUav{
        TransitionBarrier(m_particleCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        TransitionBarrier(m_particleCounters[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        TransitionBarrier(m_reverseParticleCounters[0].Get(), D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
        TransitionBarrier(m_reverseParticleCounters[1].Get(), D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };
    m_commandList->ResourceBarrier(static_cast<UINT>(countersToUav.size()), countersToUav.data());

    auto createUav = [&](UINT bufferIndex, UINT descriptorIndex)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = MaxParticles;
        uav.Buffer.StructureByteStride = sizeof(ParticleGpu);
        uav.Buffer.CounterOffsetInBytes = 0;
        m_device->CreateUnorderedAccessView(m_particleBuffers[bufferIndex].Get(),m_particleCounters[bufferIndex].Get(), &uav, CpuSrv(descriptorIndex));
    };

    createUav(0, 10);
    createUav(1, 11);
    createUav(1, 12);
    createUav(0, 13);

    auto createReverseUav = [&](UINT bufferIndex, UINT descriptorIndex)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = MaxParticles;
        uav.Buffer.StructureByteStride = sizeof(ParticleGpu);
        uav.Buffer.CounterOffsetInBytes = 0;
        m_device->CreateUnorderedAccessView(m_reverseParticleBuffers[bufferIndex].Get(),
            m_reverseParticleCounters[bufferIndex].Get(), &uav, CpuSrv(descriptorIndex));
    };
    createReverseUav(0, 19);
    createReverseUav(1, 20);
    createReverseUav(1, 21);
    createReverseUav(0, 22);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = MaxParticles;
    srv.Buffer.StructureByteStride = sizeof(ParticleGpu);
    m_device->CreateShaderResourceView(m_particleBuffers[0].Get(), &srv, CpuSrv(14));
    m_device->CreateShaderResourceView(m_particleBuffers[1].Get(), &srv, CpuSrv(15));
    m_device->CreateShaderResourceView(m_reverseParticleBuffers[0].Get(), &srv, CpuSrv(23));
    m_device->CreateShaderResourceView(m_reverseParticleBuffers[1].Get(), &srv, CpuSrv(24));

    D3D12_UNORDERED_ACCESS_VIEW_DESC sortUav{};
    sortUav.Format = DXGI_FORMAT_UNKNOWN;
    sortUav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    sortUav.Buffer.NumElements = MaxParticles;
    sortUav.Buffer.StructureByteStride = sizeof(ParticleSortItem);
    m_device->CreateUnorderedAccessView(m_reverseParticleSortItems.Get(), nullptr, &sortUav, CpuSrv(25));

    D3D12_SHADER_RESOURCE_VIEW_DESC sortSrv{};
    sortSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sortSrv.Format = DXGI_FORMAT_UNKNOWN;
    sortSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sortSrv.Buffer.NumElements = MaxParticles;
    sortSrv.Buffer.StructureByteStride = sizeof(ParticleSortItem);
    m_device->CreateShaderResourceView(m_reverseParticleSortItems.Get(), &sortSrv, CpuSrv(26));

    ID3D12DescriptorHeap* heaps[]{ m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    const ParticleSimulationConstants initialConstants{
        0.0f, 0.0f, MaxParticles, 0.0f,{ 0.0f, 12.0f, 8.0f }, 6.0f
    };
    m_commandList->SetComputeRootSignature(m_particleComputeRootSignature.Get());
    m_commandList->SetPipelineState(m_particleInitializePso.Get());
    m_commandList->SetComputeRootConstantBufferView(0, UploadConstants(&initialConstants, sizeof(initialConstants)));
    m_commandList->SetComputeRootDescriptorTable(1, GpuSrv(12));
    m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER initializedParticles{};
    initializedParticles.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    initializedParticles.UAV.pResource = m_particleBuffers[0].Get();
    m_commandList->ResourceBarrier(1, &initializedParticles);

    const ParticleSimulationConstants reverseInitialConstants{
        0.0f, 0.0f, MaxParticles, 0.0f,{ 0.0f, 12.0f, 8.0f }, 4.0f
    };
    m_commandList->SetPipelineState(m_reverseParticleInitializePso.Get());
    m_commandList->SetComputeRootConstantBufferView(
        0, UploadConstants(&reverseInitialConstants, sizeof(reverseInitialConstants)));
    // Таблица 21–22 связывает u1 с reverse buffer A.
    m_commandList->SetComputeRootDescriptorTable(1, GpuSrv(21));
    m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);

    initializedParticles.UAV.pResource = m_reverseParticleBuffers[0].Get();
    m_commandList->ResourceBarrier(1, &initializedParticles);
}

void RenderingSystem::LoadAssets()
{
    m_cubeMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::CubeData());
    m_billboardMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::BillboardData());
    m_tessellationMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::TessellatedQuadData());
    m_objMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::LoadObj(m_runtimeDirectory / L"assets/showcase.obj"));
    m_gatoMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::LoadObj(m_runtimeDirectory / L"assets/Gato.obj"));
    m_albedoTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/checker.ppm", CpuSrv(4));
    m_normalTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/normal.ppm", CpuSrv(5));
    m_displacementTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/displacement.ppm", CpuSrv(6));
    m_gatoTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/Gato.ppm", CpuSrv(7));
    m_device->CopyDescriptorsSimple(1, CpuSrv(8), CpuSrv(5), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CopyDescriptorsSimple(1, CpuSrv(9), CpuSrv(6), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_irradianceMap.LoadDds(m_device.Get(), m_commandList.Get(),
                            m_runtimeDirectory / L"assets/ibl_irradiance.dds", CpuSrv(16));
    m_prefilteredEnvironmentMap.LoadDds(m_device.Get(), m_commandList.Get(),
                                        m_runtimeDirectory / L"assets/ibl_prefiltered.dds", CpuSrv(17));
    m_brdfIntegrationMap.LoadDds(m_device.Get(), m_commandList.Get(),
                                 m_runtimeDirectory / L"assets/ibl_brdf_lut.dds", CpuSrv(18));
}

void RenderingSystem::BuildScene()
{
    auto addCube = [&](XMFLOAT3 position, XMFLOAT3 scale, XMFLOAT4 color,
                       SceneMesh mesh = SceneMesh::Cube, float yaw = 0.0f,
                       XMFLOAT4 uvParameters = XMFLOAT4{ 2.0f, 2.0f, 0.06f, 0.025f },
                       XMFLOAT4 materialParameters = XMFLOAT4{ 0.0f, 0.65f, 1.0f, 0.0f })
    {
        SceneObject object;
        XMMATRIX world = XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixRotationY(yaw) *XMMatrixTranslation(position.x, position.y, position.z);
        XMStoreFloat4x4(&object.world, world);
        object.mesh = mesh;
        MeshFor(object).Bounds().Transform(object.bounds, world); 
        const float horizontalRadius = std::hypot(object.bounds.Extents.x, object.bounds.Extents.z);
        object.bounds.Extents.x = horizontalRadius;
        object.bounds.Extents.z = horizontalRadius;
        object.color = color;
        object.materialParameters = materialParameters;
        object.uvParameters = uvParameters;
        object.textureTableStart = mesh == SceneMesh::Gato ? 7u : 4u;
        m_objects.push_back(object);
    };

    addCube({ 0,-1,0 }, { 200,1,200 }, { .10f,.10f,.10f,1 });

    addCube({ 0,.0f,0 }, { 5,.5f,5 }, { .10f,.10f,.10f,1 });
    addCube({ 0,14.5f,30 }, { 28,.5f,80 }, { .30f,.32f,.36f,1 });

    for (int z = 0; z < 14; ++z)
    {
        const float zz = static_cast<float>(z * 7);
        addCube({ -9,3.5f,zz }, { 1.0f,7.0f,1.0f }, { .7f,.65f,.52f,1 });
        addCube({  9,3.5f,zz }, { 1.0f,7.0f,1.0f }, { .7f,.65f,.52f,1 });
    }
    addCube({ 0,1.1f,8 }, { 2.2f,2.2f,2.2f }, { .8f,.45f,.18f,1 },
            SceneMesh::Showcase, 0.0f, { 2.0f,2.0f,.06f,.025f }, { .72f,.22f,1.0f,0.0f });
    addCube({ 4.5f,-.13f,9 }, { .09f,.09f,.09f }, { 1,1,1,1 },
              SceneMesh::Gato, XM_PI, { 1,1,0.04f,0.02f }, { .0f,.4f,1.0f,0.0f });

    uint32_t state = 0x12345678u;
    auto random01 = [&]() { state = state * 1664525u + 1013904223u; return (state >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < 2048; ++i)
    {
        const float x = (random01() - .5f) * 220.0f;
        const float z = (random01() - .2f) * 220.0f;
        const float size = .25f + random01() * .65f;
        addCube({ x, size, z }, { size, size * 2.0f, size },
                  { .2f + random01() * .7f, .25f + random01() * .65f, .3f + random01() * .6f, 1 });
    }

    m_sceneBounds.reserve(m_objects.size());
    for (uint32_t i = 0; i < m_objects.size(); ++i)
        m_sceneBounds.push_back({ m_objects[i].bounds, i });
    
    m_octree = std::make_unique<Octree>(BoundingBox({ 0, 30, 40 }, { 150, 80, 170 }));
    m_octree->Build(m_sceneBounds);
}

const Mesh& RenderingSystem::MeshFor(const SceneObject& object) const
{
    switch (object.mesh)
    {
    case SceneMesh::Showcase: return m_objMesh;
    case SceneMesh::Gato: return m_gatoMesh;
    default: return m_cubeMesh;
    }
}

void RenderingSystem::UpdateLods(const Camera& camera)
{
    constexpr float spriteDistance = 25.0f;
    constexpr float hiddenDistance = 60.0f;
    const XMFLOAT3 cameraPositionFloat = camera.Position();
    const XMVECTOR cameraPosition = XMLoadFloat3(&cameraPositionFloat);
    XMMATRIX cameraRotation = XMMatrixInverse(nullptr, camera.View());
    cameraRotation.r[3] = XMVectorSet(0, 0, 0, 1);

    for (SceneObject& object : m_objects)
    {
        const float radius = std::sqrt(object.bounds.Extents.x * object.bounds.Extents.x +
                                       object.bounds.Extents.y * object.bounds.Extents.y +
                                       object.bounds.Extents.z * object.bounds.Extents.z);
        const float distance = std::max(0.0f,
            XMVectorGetX(XMVector3Length(XMLoadFloat3(&object.bounds.Center) - cameraPosition)) - radius);
        object.lod = distance < spriteDistance ? LodLevel::Mesh
                   : distance < hiddenDistance ? LodLevel::Sprite
                   : LodLevel::FarHidden;

        if (object.lod == LodLevel::Sprite)
        {
            const XMFLOAT3 center = object.bounds.Center;
            const float width = 2.0f * object.bounds.Extents.x;
            const float height = 2.0f * object.bounds.Extents.y;
            XMStoreFloat4x4(&object.spriteWorld,
                XMMatrixScaling(width, height, 1.0f) * cameraRotation *
                XMMatrixTranslation(center.x, center.y, center.z));
        }
    }
}

void RenderingSystem::UpdateVisibility(const Camera& camera)
{
    m_visibleIndices.clear();
    // Самый быстрый из прежних режимов теперь используется постоянно.
    m_octree->Query(camera.WorldFrustum(), m_visibleIndices);
    m_visibleIndices.erase(std::remove_if(m_visibleIndices.begin(), m_visibleIndices.end(),
        [&](uint32_t index) { return m_objects[index].lod >= LodLevel::Hidden; }), m_visibleIndices.end());
}

void RenderingSystem::UpdateShadowCascades(const Camera& camera)
{
    constexpr float splitLambda = 0.75f; 
    constexpr float casterMargin = 120.0f; 
    const float nearPlane = camera.NearPlane();
    const float farPlane = camera.FarPlane();
    const float tanHalfFov = std::tan(camera.FieldOfView() * 0.5f);
    const XMMATRIX inverseView = XMMatrixInverse(nullptr, camera.View());
    const XMVECTOR lightDirection = XMVector3Normalize(XMLoadFloat3(&SunDirection));
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR lightRight = XMVector3Normalize(XMVector3Cross(worldUp, lightDirection));
    const XMVECTOR lightUp = XMVector3Normalize(XMVector3Cross(lightDirection, lightRight));

    std::array<float, CascadeCount> splits{};
    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        const float ratio = static_cast<float>(cascade + 1) / static_cast<float>(CascadeCount);
        const float logarithmic = nearPlane * std::pow(farPlane / nearPlane, ratio); 
        const float uniform = nearPlane + (farPlane - nearPlane) * ratio; 
        splits[cascade] = splitLambda * logarithmic + (1.0f - splitLambda) * uniform;
    }
    m_cascadeSplits = { splits[0], splits[1], splits[2], splits[3] };

    float cascadeNear = nearPlane;
    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        const float cascadeFar = splits[cascade];
        std::array<XMVECTOR, 8> corners{};
        UINT cornerIndex = 0;
        for (float depth : { cascadeNear, cascadeFar })
        {
            const float halfHeight = depth * tanHalfFov;
            const float halfWidth = halfHeight * camera.AspectRatio();
            for (float y : { -halfHeight, halfHeight })
            {
                for (float x : { -halfWidth, halfWidth })
                    corners[cornerIndex++] = XMVector3TransformCoord(XMVectorSet(x, y, depth, 1.0f), inverseView);
            }
        }

        XMVECTOR center = XMVectorZero();
        for (const XMVECTOR corner : corners)
            center += corner;
        center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const XMVECTOR corner : corners)
            radius = std::max(radius, XMVectorGetX(XMVector3Length(corner - center)));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(ShadowMapSize);
        const float centerRight = XMVectorGetX(XMVector3Dot(center, lightRight));
        const float centerUp = XMVectorGetX(XMVector3Dot(center, lightUp));
        const float snappedRight = std::round(centerRight / worldUnitsPerTexel) * worldUnitsPerTexel;
        const float snappedUp = std::round(centerUp / worldUnitsPerTexel) * worldUnitsPerTexel;
        center += lightRight * (snappedRight - centerRight);
        center += lightUp * (snappedUp - centerUp);
        const XMVECTOR lightPosition = center - lightDirection * (radius + casterMargin);
        const XMMATRIX lightView = XMMatrixLookToLH(lightPosition, lightDirection, lightUp);
        const float shadowFar = 2.0f * radius + 2.0f * casterMargin;
        const XMMATRIX lightProjection = XMMatrixOrthographicOffCenterLH(
            -radius, radius, -radius, radius, 0.1f, shadowFar);
        XMStoreFloat4x4(&m_shadowViewProjections[cascade], lightView * lightProjection); // Матрица уходит в HLSL.

        auto& visible = m_shadowVisibleIndices[cascade];
        visible.clear();
        visible.reserve(m_objects.size() / 2);
        for (uint32_t objectIndex = 0; objectIndex < m_objects.size(); ++objectIndex)
        {
            if (m_objects[objectIndex].lod >= LodLevel::Hidden) continue;
            BoundingBox lightBounds;
            m_objects[objectIndex].bounds.Transform(lightBounds, lightView);
            const XMFLOAT3 minimum{
                lightBounds.Center.x - lightBounds.Extents.x,
                lightBounds.Center.y - lightBounds.Extents.y,
                lightBounds.Center.z - lightBounds.Extents.z
            };
            const XMFLOAT3 maximum{
                lightBounds.Center.x + lightBounds.Extents.x,
                lightBounds.Center.y + lightBounds.Extents.y,
                lightBounds.Center.z + lightBounds.Extents.z
            };
            if (maximum.x >= -radius and minimum.x <= radius and
                maximum.y >= -radius and minimum.y <= radius and
                maximum.z >= 0.1f and minimum.z <= shadowFar)
                visible.push_back(objectIndex);
        }
        cascadeNear = cascadeFar;
    }
}

void RenderingSystem::UpdateTessellationCache(const Camera& camera, float totalTime)
{
    auto verticesToOutput = TransitionBarrier(m_cachedTessVertices.Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, D3D12_RESOURCE_STATE_STREAM_OUT);
    auto counterToCopy = TransitionBarrier(m_cachedTessFilledSize.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    const D3D12_RESOURCE_BARRIER beforeCapture[]{ verticesToOutput, counterToCopy };
    m_commandList->ResourceBarrier(static_cast<UINT>(std::size(beforeCapture)), beforeCapture);
    m_commandList->CopyBufferRegion(m_cachedTessFilledSize.Get(), 0,m_tessFilledSizeReset.Get(), 0, sizeof(UINT));
    auto counterToOutput = TransitionBarrier(m_cachedTessFilledSize.Get(),D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_STREAM_OUT);
    m_commandList->ResourceBarrier(1, &counterToOutput);

    const D3D12_STREAM_OUTPUT_BUFFER_VIEW output{
        m_cachedTessVertices->GetGPUVirtualAddress(), CachedVertexBufferBytes,
        m_cachedTessFilledSize->GetGPUVirtualAddress()
    };
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
    m_commandList->SOSetTargets(0, 1, &output);
    m_commandList->SetGraphicsRootSignature(m_geometryRootSignature.Get());
    m_commandList->SetPipelineState(m_tessCapturePso.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(4));
    m_tessellationMesh.Bind(m_commandList.Get());
    ObjectConstants constants{};
    XMStoreFloat4x4(&constants.world, XMMatrixScaling(5, 1, 5) * XMMatrixTranslation(0, .15f, 17));
    XMStoreFloat4x4(&constants.viewProjection, camera.ViewProjection());
    const XMFLOAT3 cameraPosition = camera.Position();
    constants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
    constants.uvParameters = { 6.0f, 6.0f, 0.03f, 0.0f };
    constants.materialColor = { .45f, .7f, .9f, 1.0f };
    constants.materialParameters = { .1f, .32f, 1.0f, 0.0f };
    constants.tessellationParameters = { 12.0f, 1.0f, 3.0f, 45.0f };
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->DrawIndexedInstanced(m_tessellationMesh.IndexCount(), 1, 0, 0, 0);
    m_commandList->SOSetTargets(0, 0, nullptr);

    auto verticesToInput = TransitionBarrier(m_cachedTessVertices.Get(),
        D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    auto counterToInput = TransitionBarrier(m_cachedTessFilledSize.Get(),
        D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    auto argsToWrite = TransitionBarrier(m_cachedTessDrawArgs.Get(),
        D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    const D3D12_RESOURCE_BARRIER afterCapture[]{ verticesToInput, counterToInput, argsToWrite };
    m_commandList->ResourceBarrier(static_cast<UINT>(std::size(afterCapture)), afterCapture);

    m_commandList->SetComputeRootSignature(m_tessDrawArgsRootSignature.Get());
    m_commandList->SetPipelineState(m_tessDrawArgsPso.Get());
    m_commandList->SetComputeRootShaderResourceView(0, m_cachedTessFilledSize->GetGPUVirtualAddress());
    m_commandList->SetComputeRootUnorderedAccessView(1, m_cachedTessDrawArgs->GetGPUVirtualAddress());
    m_commandList->Dispatch(1, 1, 1);
    auto argsToDraw = TransitionBarrier(m_cachedTessDrawArgs.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    m_commandList->ResourceBarrier(1, &argsToDraw);
}

void RenderingSystem::DrawCachedTessellation(const ObjectConstants& constants)
{
    m_commandList->IASetVertexBuffers(0, 1, &m_cachedTessVertexView);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->ExecuteIndirect(m_tessDrawCommandSignature.Get(), 1,m_cachedTessDrawArgs.Get(), 0, nullptr, 0);
}

void RenderingSystem::RenderShadowMaps(const Camera& camera, float totalTime)
{
    auto toDepth = TransitionBarrier(m_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);
    m_commandList->ResourceBarrier(1, &toDepth);
    m_commandList->RSSetViewports(1, &m_shadowViewport); 
    m_commandList->RSSetScissorRects(1, &m_shadowScissor); 
    const UINT dsvIncrement = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    const XMFLOAT3 cameraPosition = camera.Position();

    for (UINT cascade = 0; cascade < CascadeCount; ++cascade)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowDsvHeap->GetCPUDescriptorHandleForHeapStart();
        dsv.ptr += static_cast<SIZE_T>(cascade) * dsvIncrement;
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
        m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr); 
        m_commandList->SetGraphicsRootSignature(m_shadowRootSignature.Get());
        m_commandList->SetPipelineState(m_shadowPso.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); 

        for (uint32_t objectIndex : m_shadowVisibleIndices[cascade])
        {
            const SceneObject& object = m_objects[objectIndex];
            const bool isSprite = object.lod == LodLevel::Sprite;
            m_commandList->SetPipelineState(isSprite ? m_spriteShadowPso.Get() : m_shadowPso.Get());
            const Mesh& mesh = isSprite ? m_billboardMesh : MeshFor(object);
            mesh.Bind(m_commandList.Get());
            ShadowConstants constants{ isSprite ? object.spriteWorld : object.world,
                                       m_shadowViewProjections[cascade] };
            m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
            for (const Submesh& submesh : mesh.Submeshes())
                m_commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        }

        // Сетка берётся из GPU-буфера, заполненного Stream Output.
        m_commandList->SetGraphicsRootSignature(m_geometryRootSignature.Get());
        m_commandList->SetPipelineState(m_cachedShadowPso.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(4)); 
        ObjectConstants tessConstants{};
        XMStoreFloat4x4(&tessConstants.world, XMMatrixScaling(5, 1, 5) * XMMatrixTranslation(0, .15f, 17));
        tessConstants.viewProjection = m_shadowViewProjections[cascade];
        tessConstants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
        tessConstants.uvParameters = { 6.0f, 6.0f, 0.03f, 0.0f };
        tessConstants.materialColor = { 1,1,1,1 };
        tessConstants.materialParameters = { .1f,.32f,1.0f,0.0f };
        tessConstants.tessellationParameters = { 12.0f, 1.0f, 3.0f, 45.0f };
        DrawCachedTessellation(tessConstants);
    }

    auto toShaderResource = TransitionBarrier(m_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &toShaderResource);
}

D3D12_GPU_VIRTUAL_ADDRESS RenderingSystem::UploadConstants(const void* data, size_t size)
{
    const UINT64 aligned = AlignConstantBuffer(size); 
    if (m_constantOffset + aligned > (m_frameIndex + 1) * ConstantsPerFrame)
        throw std::runtime_error("Per-frame constant buffer exhausted");
    memcpy(m_constantMapped + m_constantOffset, data, size); 
    const auto address = m_constantUpload->GetGPUVirtualAddress() + m_constantOffset;
    m_constantOffset += aligned;
    return address;
}

void RenderingSystem::UpdateParticles(float deltaTime)
{
    const float simulationStep = std::clamp(deltaTime, 0.0f, 0.05f);
    m_particleTime += simulationStep;

    const UINT inputIndex = m_particleReadBuffer;
    const UINT outputIndex = 1u - inputIndex;
    if (m_particleBufferStates[inputIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto inputToUav = TransitionBarrier(m_particleBuffers[inputIndex].Get(),
            m_particleBufferStates[inputIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &inputToUav);
        m_particleBufferStates[inputIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (m_particleBufferStates[outputIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto outputToUav = TransitionBarrier(m_particleBuffers[outputIndex].Get(),
            m_particleBufferStates[outputIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &outputToUav);
        m_particleBufferStates[outputIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Перед Append обнуляется только скрытый счётчик выходного UAV; содержимое буфера
    // перезаписывается compute shader-ом целиком.
    auto counterToCopy = TransitionBarrier(m_particleCounters[outputIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    m_commandList->ResourceBarrier(1, &counterToCopy);
    m_commandList->CopyBufferRegion(m_particleCounters[outputIndex].Get(), 0,
                                    m_particleCounterUpload.Get(), 0, sizeof(UINT));
    auto counterToUav = TransitionBarrier(m_particleCounters[outputIndex].Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_commandList->ResourceBarrier(1, &counterToUav);

    const ParticleSimulationConstants constants{
        simulationStep, m_particleTime, MaxParticles, 0.0f,
        { 0.0f, 12.0f, 8.0f }, 6.0f
    };
    m_commandList->SetComputeRootSignature(m_particleComputeRootSignature.Get());
    m_commandList->SetPipelineState(m_particleComputePso.Get());
    m_commandList->SetComputeRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->SetComputeRootDescriptorTable(1, GpuSrv(inputIndex == 0 ? 10 : 12));
    m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_particleBuffers[outputIndex].Get();
    m_commandList->ResourceBarrier(1, &uavBarrier);
    auto outputToSrv = TransitionBarrier(m_particleBuffers[outputIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &outputToSrv);
    m_particleBufferStates[outputIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_particleReadBuffer = outputIndex;
}

void RenderingSystem::UpdateReverseParticles(float deltaTime)
{
    const float simulationStep = std::clamp(deltaTime, 0.0f, 0.05f);
    const UINT inputIndex = m_reverseParticleReadBuffer;
    const UINT outputIndex = 1u - inputIndex;

    if (m_reverseParticleBufferStates[inputIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto inputToUav = TransitionBarrier(m_reverseParticleBuffers[inputIndex].Get(),
            m_reverseParticleBufferStates[inputIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &inputToUav);
        m_reverseParticleBufferStates[inputIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (m_reverseParticleBufferStates[outputIndex] != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto outputToUav = TransitionBarrier(m_reverseParticleBuffers[outputIndex].Get(),
            m_reverseParticleBufferStates[outputIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &outputToUav);
        m_reverseParticleBufferStates[outputIndex] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    auto counterToCopy = TransitionBarrier(m_reverseParticleCounters[outputIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_DEST);
    m_commandList->ResourceBarrier(1, &counterToCopy);
    m_commandList->CopyBufferRegion(m_reverseParticleCounters[outputIndex].Get(), 0,
                                    m_particleCounterUpload.Get(), 0, sizeof(UINT));
    auto counterToUav = TransitionBarrier(m_reverseParticleCounters[outputIndex].Get(),
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_commandList->ResourceBarrier(1, &counterToUav);

    const ParticleSimulationConstants constants{
        simulationStep, m_particleTime, MaxParticles, 0.0f,
        { 0.0f, 12.0f, 8.0f }, 4.0f
    };
    m_commandList->SetComputeRootSignature(m_particleComputeRootSignature.Get());
    m_commandList->SetPipelineState(m_reverseParticleComputePso.Get());
    m_commandList->SetComputeRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->SetComputeRootDescriptorTable(1, GpuSrv(inputIndex == 0 ? 19 : 21));
    m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier{};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_reverseParticleBuffers[outputIndex].Get();
    m_commandList->ResourceBarrier(1, &uavBarrier);
    auto outputToSrv = TransitionBarrier(m_reverseParticleBuffers[outputIndex].Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &outputToSrv);
    m_reverseParticleBufferStates[outputIndex] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    m_reverseParticleReadBuffer = outputIndex;
}

void RenderingSystem::SortReverseParticles(const Camera& camera)
{
    static_assert((MaxParticles & (MaxParticles - 1)) == 0,
                  "Bitonic sort requires a power-of-two particle count");

    if (m_reverseParticleSortState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    {
        auto sortItemsToUav = TransitionBarrier(m_reverseParticleSortItems.Get(),
            m_reverseParticleSortState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        m_commandList->ResourceBarrier(1, &sortItemsToUav);
        m_reverseParticleSortState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    const XMFLOAT3 cameraPosition = camera.Position();
    ParticleSortConstants constants{
        cameraPosition, MaxParticles, 0, 0, { 0.0f, 0.0f }
    };
    m_commandList->SetComputeRootSignature(m_particleSortRootSignature.Get());
    m_commandList->SetPipelineState(m_particleSortBuildPso.Get());
    m_commandList->SetComputeRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->SetComputeRootDescriptorTable(
        1, GpuSrv(m_reverseParticleReadBuffer == 0 ? 23 : 24));
    m_commandList->SetComputeRootDescriptorTable(2, GpuSrv(25));
    m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);

    D3D12_RESOURCE_BARRIER sortBarrier{};
    sortBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    sortBarrier.UAV.pResource = m_reverseParticleSortItems.Get();
    m_commandList->ResourceBarrier(1, &sortBarrier);

    m_commandList->SetPipelineState(m_particleSortStepPso.Get());
    for (UINT stageK = 2; stageK <= MaxParticles; stageK <<= 1)
    {
        for (UINT stageJ = stageK >> 1; stageJ > 0; stageJ >>= 1)
        {
            constants.stageK = stageK;
            constants.stageJ = stageJ;
            m_commandList->SetComputeRootConstantBufferView(
                0, UploadConstants(&constants, sizeof(constants)));
            m_commandList->Dispatch((MaxParticles + 63) / 64, 1, 1);
            m_commandList->ResourceBarrier(1, &sortBarrier);
        }
    }

    auto sortItemsToSrv = TransitionBarrier(m_reverseParticleSortItems.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    m_commandList->ResourceBarrier(1, &sortItemsToSrv);
    m_reverseParticleSortState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
}

void RenderingSystem::DrawParticles(const Camera& camera)
{
    ParticleDrawConstants constants{};
    XMStoreFloat4x4(&constants.viewProjection, camera.ViewProjection());
    const XMFLOAT3 cameraForwardFloat = camera.Direction();
    const XMVECTOR cameraForward = XMLoadFloat3(&cameraForwardFloat);
    const XMVECTOR cameraRight = XMVector3Normalize(
        XMVector3Cross(XMVectorSet(0, 1, 0, 0), cameraForward));
    const XMVECTOR cameraUp = XMVector3Normalize(XMVector3Cross(cameraForward, cameraRight));
    XMStoreFloat4(&constants.cameraRight, cameraRight);
    XMStoreFloat4(&constants.cameraUp, cameraUp);
    constants.colorAndSize = { 0.18f, 0.62f, 1.0f, 0.045f };
    constants.parameters = { 1.0f, 0.0f, 0.0f, 0.0f };

    m_commandList->SetGraphicsRootSignature(m_particleDrawRootSignature.Get());
    m_commandList->SetPipelineState(m_particleDrawPso.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(m_particleReadBuffer == 0 ? 14 : 15));
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->DrawInstanced(MaxParticles, 1, 0, 0);
}

void RenderingSystem::DrawTransparentParticles(const Camera& camera)
{
    ParticleDrawConstants constants{};
    XMStoreFloat4x4(&constants.viewProjection, camera.ViewProjection());
    const XMFLOAT3 cameraForwardFloat = camera.Direction();
    const XMVECTOR cameraForward = XMLoadFloat3(&cameraForwardFloat);
    const XMVECTOR cameraRight = XMVector3Normalize(
        XMVector3Cross(XMVectorSet(0, 1, 0, 0), cameraForward));
    const XMVECTOR cameraUp = XMVector3Normalize(XMVector3Cross(cameraForward, cameraRight));
    XMStoreFloat4(&constants.cameraRight, cameraRight);
    XMStoreFloat4(&constants.cameraUp, cameraUp);
    constants.colorAndSize = { 1.0f, 0.05f, 0.05f, 0.18f };
    constants.parameters = { 0.35f, 0.0f, 0.0f, 0.0f };

    m_commandList->SetGraphicsRootSignature(m_transparentParticleDrawRootSignature.Get());
    m_commandList->SetPipelineState(m_transparentParticleDrawPso.Get());
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
    m_commandList->SetGraphicsRootDescriptorTable(
        1, GpuSrv(m_reverseParticleReadBuffer == 0 ? 23 : 24));
    m_commandList->SetGraphicsRootDescriptorTable(2, GpuSrv(26));
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->DrawInstanced(MaxParticles, 1, 0, 0);
}

void RenderingSystem::PopulateCommandList(const Camera& camera, float totalTime, float deltaTime,
                                          bool refreshTessellation)
{
    ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "Reset command allocator");
    ThrowIfFailed(m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr), "Reset command list");
    m_constantOffset = static_cast<UINT64>(m_frameIndex) * ConstantsPerFrame; 
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissor);
    ID3D12DescriptorHeap* heaps[]{ m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps); 

    UpdateParticles(deltaTime);
    UpdateReverseParticles(deltaTime);
    SortReverseParticles(camera);
    if (refreshTessellation)
        UpdateTessellationCache(camera, totalTime);
    RenderShadowMaps(camera, totalTime);
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissor);
    m_gbuffer.BeginGeometry(m_commandList.Get());
    m_commandList->SetGraphicsRootSignature(m_geometryRootSignature.Get());
    m_commandList->SetPipelineState(m_geometryPso.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(4)); 

    const XMMATRIX viewProjection = camera.ViewProjection();
    const XMFLOAT3 cameraPosition = camera.Position();
    for (uint32_t index : m_visibleIndices)
    {
        const SceneObject& object = m_objects[index];
        const bool isSprite = object.lod == LodLevel::Sprite;
        m_commandList->SetPipelineState(isSprite ? m_spritePso.Get() : m_geometryPso.Get());
        ObjectConstants constants{};
        constants.world = isSprite ? object.spriteWorld : object.world;
        XMStoreFloat4x4(&constants.viewProjection, viewProjection);
        constants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
        constants.uvParameters = object.uvParameters;
        constants.materialParameters = object.materialParameters;
        constants.tessellationParameters = { 1, 1, 1, 1 };
        const Mesh& sourceMesh = MeshFor(object);
        const Mesh& mesh = isSprite ? m_billboardMesh : sourceMesh;
        m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(object.textureTableStart));
        mesh.Bind(m_commandList.Get());
        for (const Submesh& submesh : mesh.Submeshes())
        {
            const uint32_t materialIndex = isSprite ? sourceMesh.Submeshes().front().materialIndex
                                                    : submesh.materialIndex;
            const XMFLOAT4 material = materialIndex < sourceMesh.Materials().size()
                ? sourceMesh.Materials()[materialIndex].diffuse : XMFLOAT4{ 1,1,1,1 };
            constants.materialColor = {
                object.color.x * material.x, object.color.y * material.y,
                object.color.z * material.z, object.color.w * material.w
            };
            m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
            m_commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        }
    }

    m_commandList->SetPipelineState(m_cachedTessellationPso.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(4));
    ObjectConstants tessConstants{};
    XMStoreFloat4x4(&tessConstants.world, XMMatrixScaling(5, 1, 5) * XMMatrixTranslation(0, .15f, 17));
    XMStoreFloat4x4(&tessConstants.viewProjection, viewProjection);
    tessConstants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
    tessConstants.uvParameters = { 6.0f, 6.0f, 0.03f, 0.0f };
    tessConstants.materialColor = { .45f,.7f,.9f,1 };
    tessConstants.materialParameters = { .1f,.32f,1.0f,0.0f };
    tessConstants.tessellationParameters = { 12.0f, 1.0f, 3.0f, 45.0f };
    DrawCachedTessellation(tessConstants);
    DrawParticles(camera);
    m_gbuffer.EndGeometry(m_commandList.Get()); 
    auto backBarrier = TransitionBarrier(m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &backBarrier);
    D3D12_CPU_DESCRIPTOR_HANDLE backRtv = m_swapChainRtvHeap->GetCPUDescriptorHandleForHeapStart();
    backRtv.ptr += static_cast<SIZE_T>(m_frameIndex) * m_rtvIncrement;
    m_commandList->OMSetRenderTargets(1, &backRtv, FALSE, nullptr); 
    const float clear[4]{ .01f,.015f,.025f,1 };
    m_commandList->ClearRenderTargetView(backRtv, clear, 0, nullptr);
    m_commandList->SetGraphicsRootSignature(m_lightingRootSignature.Get());
    m_commandList->SetPipelineState(m_lightingPso.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    LightingConstants lighting{};
    XMStoreFloat4x4(&lighting.inverseViewProjection, XMMatrixInverse(nullptr, viewProjection));
    lighting.shadowViewProjections = m_shadowViewProjections;
    lighting.cameraAndLightCount = { cameraPosition.x, cameraPosition.y, cameraPosition.z, 8.0f }; 
    lighting.iblParameters = { 4.0f, 1.0f, 1.0f, 0.0f };
    lighting.cascadeSplits = m_cascadeSplits;
    lighting.shadowParameters = { 1.0f / static_cast<float>(ShadowMapSize), 0.00035f, 0.0025f, 0.0f };
    const XMFLOAT3 cameraForward = camera.Direction();
    lighting.cameraForward = { cameraForward.x, cameraForward.y, cameraForward.z, 0.0f };
    lighting.postProcessParameters = {
        static_cast<float>(m_postProcessMode),
        1.0f / static_cast<float>(m_width),
        1.0f / static_cast<float>(m_height),
        0.0f
    };

    // Солнце
    lighting.lights[SunLightIndex] = {
        { 0, 0, 0, 0 },
        { SunDirection.x, SunDirection.y, SunDirection.z, DirectionalLightType },
        SunColorAndIntensity,
        {}, {}
    };

    // Источники света
    lighting.lights[1] = { {-6,5,8,13}, {0,0,0,1}, {1.0f,.15f,.08f,10}, {}, {} };
    lighting.lights[2] = { { 6,4,18,14}, {0,0,0,1}, {.08f,.25f,1.0f,11}, {}, {} };
    lighting.lights[3] = { {-5,3,31,12}, {0,0,0,1}, {.2f,1.0f,.35f,8}, {}, {} };
    lighting.lights[4] = { { 5,5,46,15}, {0,0,0,1}, {1.0f,.35f,.75f,10}, {}, {} };
    lighting.lights[5] = { { 0,8,5,35}, {0,-.75f,.65f,2}, {1.0f,.85f,.55f,16}, {.94f,.76f,0,0}, {} };
    lighting.lights[6] = { {-8,6,62,18}, {0,0,0,1}, {.2f,.65f,1.0f,10}, {}, {} };
    lighting.lights[7] = { { 8,4,78,16}, {0,0,0,1}, {1.0f,.55f,.15f,9}, {}, {} };
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&lighting, sizeof(lighting))); // b0
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(0)); 
    // Full-screen quad строится в VS по SV_VertexID, поэтому vertex/index buffer не нужен.
    m_commandList->IASetVertexBuffers(0, 0, nullptr);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->DrawInstanced(6, 1, 0, 0);

    // Полупрозрачные частицы рисуются после deferred lighting прямо в back buffer.
    // Depth test использует глубину непрозрачной сцены, но depth write выключен в PSO.
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneDepth = m_gbuffer.Dsv();
    m_commandList->OMSetRenderTargets(1, &backRtv, FALSE, &sceneDepth);
    DrawTransparentParticles(camera);

    backBarrier = TransitionBarrier(m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &backBarrier);
    ThrowIfFailed(m_commandList->Close(), "Close frame command list");
}

void RenderingSystem::Render(const Camera& camera, float totalTime, float deltaTime)
{
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex(); 
    WaitForFrame(m_frameIndex); 
    const XMFLOAT3 cameraPosition = camera.Position();
    const bool refreshTessellation = !m_tessCacheValid or cameraPosition.x != m_cachedTessCameraPosition.x || cameraPosition.y != m_cachedTessCameraPosition.y || cameraPosition.z != m_cachedTessCameraPosition.z;
    UpdateLods(camera);
    UpdateVisibility(camera);
    UpdateShadowCascades(camera);
    PopulateCommandList(camera, totalTime, deltaTime, refreshTessellation);
    ID3D12CommandList* lists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    if (refreshTessellation)
    {
        m_cachedTessCameraPosition = cameraPosition;
        m_tessCacheValid = true;
    }
    ThrowIfFailed(m_swapChain->Present(1, 0), "Present"); 
    const UINT64 value = ++m_nextFenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), value), "Signal frame fence"); 
    m_frameFenceValues[m_frameIndex] = value;
}

void RenderingSystem::WaitForFrame(UINT frameIndex)
{
    const UINT64 value = m_frameFenceValues[frameIndex];
    if (value and m_fence->GetCompletedValue() < value)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent), "Set fence event");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

void RenderingSystem::FlushGpu()
{
    const UINT64 value = ++m_nextFenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), value), "Signal flush fence");
    if (m_fence->GetCompletedValue() < value)
    {
        ThrowIfFailed(m_fence->SetEventOnCompletion(value, m_fenceEvent), "Set flush fence event");
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderingSystem::CpuSrv(UINT index) const
{
    // CPU-handle используется при создании/копировании дескриптора в выбранную ячейку heap.
    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_srvIncrement;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderingSystem::GpuSrv(UINT index) const
{
    // GPU-handle передаётся root descriptor table и становится началом последовательности t-регистров.
    auto handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * m_srvIncrement;
    return handle;
}
