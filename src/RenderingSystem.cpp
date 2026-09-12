#include "RenderingSystem.hpp"

using namespace DirectX;

namespace
{
    D3D12_RASTERIZER_DESC DefaultRasterizer()
    {
        D3D12_RASTERIZER_DESC result{};
        result.FillMode = D3D12_FILL_MODE_SOLID;
        result.CullMode = D3D12_CULL_MODE_BACK;
        result.FrontCounterClockwise = FALSE;
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
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_ANISOTROPIC;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxAnisotropy = 8;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MinLOD = 0;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
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
        sampler.ShaderRegister = 1;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        return sampler;
    }
}

RenderingSystem::RenderingSystem(HWND window, UINT width, UINT height)
    : m_width(width), m_height(height), m_runtimeDirectory(ExecutableDirectory())
{
    m_viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
    m_scissor = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
    CreateDeviceAndSwapChain(window);
    CreateDescriptors();
    CreateRootSignatures();
    CreatePipelineStates();
    CreateConstantUpload();
    LoadAssets();
    BuildScene();

    ThrowIfFailed(m_commandList->Close(), "Close initialization command list");
    ID3D12CommandList* lists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    FlushGpu();
}

RenderingSystem::~RenderingSystem()
{
    if (m_commandQueue && m_fence)
        FlushGpu();
    if (m_constantUpload && m_constantMapped)
        m_constantUpload->Unmap(0, nullptr);
    if (m_fenceEvent)
        CloseHandle(m_fenceEvent);
}

void RenderingSystem::CreateDeviceAndSwapChain(HWND window)
{
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
    for (UINT index = 0; m_factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                               IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++index)
    {
        DXGI_ADAPTER_DESC1 description{};
        adapter->GetDesc1(&description);
        if (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
            break;
        adapter.Reset();
    }
    if (!m_device)
    {
        ComPtr<IDXGIAdapter> warp;
        ThrowIfFailed(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warp)), "Find WARP adapter");
        ThrowIfFailed(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device)),
                      "Create D3D12 device");
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)), "Create command queue");
    for (auto& allocator : m_allocators)
        ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
                      "Create command allocator");
    ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocators[0].Get(), nullptr,
                                              IID_PPV_ARGS(&m_commandList)), "Create command list");

    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = m_width;
    swapDesc.Height = m_height;
    swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = FrameCount;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swapChain;
    ThrowIfFailed(m_factory->CreateSwapChainForHwnd(m_commandQueue.Get(), window, &swapDesc, nullptr, nullptr,
                                                    &swapChain), "Create swap chain");
    ThrowIfFailed(swapChain.As(&m_swapChain), "Query swap chain 3");
    m_factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);

    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)), "Create fence");
    m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) throw std::runtime_error("CreateEventW failed");
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
    srvDesc.NumDescriptors = 16;
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
    ThrowIfFailed(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&m_shadowMap)),
        "Create cascaded shadow map");

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

    m_shadowViewport = { 0.0f, 0.0f, static_cast<float>(ShadowMapSize),
                         static_cast<float>(ShadowMapSize), 0.0f, 1.0f };
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
    geometryDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature, errors;
    HRESULT hr = D3D12SerializeRootSignature(&geometryDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                             &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                IID_PPV_ARGS(&m_geometryRootSignature)), "Create geometry root signature");

    D3D12_DESCRIPTOR_RANGE lightingRange{};
    lightingRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    lightingRange.NumDescriptors = GBuffer::TargetCount + 1;
    lightingRange.BaseShaderRegister = 0;
    lightingRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    std::array<D3D12_ROOT_PARAMETER, 2> lightingParams{};
    lightingParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    lightingParams[0].Descriptor.ShaderRegister = 0;
    lightingParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    lightingParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    lightingParams[1].DescriptorTable.NumDescriptorRanges = 1;
    lightingParams[1].DescriptorTable.pDescriptorRanges = &lightingRange;
    lightingParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC lightingDesc{};
    lightingDesc.NumParameters = static_cast<UINT>(lightingParams.size());
    lightingDesc.pParameters = lightingParams.data();
    const std::array<D3D12_STATIC_SAMPLER_DESC, 2> lightingSamplers{
        sampler, ShadowComparisonSampler()
    };
    lightingDesc.NumStaticSamplers = static_cast<UINT>(lightingSamplers.size());
    lightingDesc.pStaticSamplers = lightingSamplers.data();
    hr = D3D12SerializeRootSignature(&lightingDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
    if (FAILED(hr))
        throw std::runtime_error(errors ? static_cast<const char*>(errors->GetBufferPointer()) : "Root signature error");
    ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                IID_PPV_ARGS(&m_lightingRootSignature)), "Create lighting root signature");

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
}

ComPtr<ID3DBlob> RenderingSystem::CompileShader(const std::filesystem::path& file,
                                                const char* entry, const char* target) const
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
    const auto lightingVs = CompileShader(m_runtimeDirectory / L"shader/Lighting.hlsl", "VSMain", "vs_5_1");
    const auto lightingPs = CompileShader(m_runtimeDirectory / L"shader/Lighting.hlsl", "PSMain", "ps_5_1");
    const auto tessVs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "VSMain", "vs_5_1");
    const auto tessHs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "HSMain", "hs_5_1");
    const auto tessDs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "DSMain", "ds_5_1");
    const auto tessPs = CompileShader(m_runtimeDirectory / L"shader/Tessellation.hlsl", "PSMain", "ps_5_1");
    const auto shadowVs = CompileShader(m_runtimeDirectory / L"shader/Shadow.hlsl", "VSMain", "vs_5_1");
    const auto shadowTessVs = CompileShader(m_runtimeDirectory / L"shader/ShadowTessellation.hlsl", "VSMain", "vs_5_1");
    const auto shadowTessHs = CompileShader(m_runtimeDirectory / L"shader/ShadowTessellation.hlsl", "HSMain", "hs_5_1");
    const auto shadowTessDs = CompileShader(m_runtimeDirectory / L"shader/ShadowTessellation.hlsl", "DSMain", "ds_5_1");

    const D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, tangent), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geometry{};
    geometry.pRootSignature = m_geometryRootSignature.Get();
    geometry.VS = { geometryVs->GetBufferPointer(), geometryVs->GetBufferSize() };
    geometry.PS = { geometryPs->GetBufferPointer(), geometryPs->GetBufferSize() };
    geometry.BlendState = DefaultBlend();
    geometry.SampleMask = UINT_MAX;
    geometry.RasterizerState = DefaultRasterizer();
    geometry.DepthStencilState = DefaultDepth();
    geometry.InputLayout = { inputLayout, static_cast<UINT>(std::size(inputLayout)) };
    geometry.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geometry.NumRenderTargets = GBuffer::TargetCount;
    for (UINT i = 0; i < GBuffer::TargetCount; ++i) geometry.RTVFormats[i] = m_gbuffer.Formats()[i];
    geometry.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    geometry.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&geometry, IID_PPV_ARGS(&m_geometryPso)),
                  "Create geometry PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessellation = geometry;
    tessellation.VS = { tessVs->GetBufferPointer(), tessVs->GetBufferSize() };
    tessellation.HS = { tessHs->GetBufferPointer(), tessHs->GetBufferSize() };
    tessellation.DS = { tessDs->GetBufferPointer(), tessDs->GetBufferSize() };
    tessellation.PS = { tessPs->GetBufferPointer(), tessPs->GetBufferSize() };
    tessellation.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    tessellation.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&tessellation, IID_PPV_ARGS(&m_tessellationPso)),
                  "Create tessellation PSO");

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
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&lighting, IID_PPV_ARGS(&m_lightingPso)),
                  "Create lighting PSO");

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
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&m_shadowPso)),
                  "Create shadow PSO");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowTessellation = shadow;
    shadowTessellation.pRootSignature = m_geometryRootSignature.Get();
    shadowTessellation.VS = { shadowTessVs->GetBufferPointer(), shadowTessVs->GetBufferSize() };
    shadowTessellation.HS = { shadowTessHs->GetBufferPointer(), shadowTessHs->GetBufferSize() };
    shadowTessellation.DS = { shadowTessDs->GetBufferPointer(), shadowTessDs->GetBufferSize() };
    shadowTessellation.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    shadowTessellation.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(&shadowTessellation,
                  IID_PPV_ARGS(&m_shadowTessellationPso)), "Create tessellated shadow PSO");
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

void RenderingSystem::LoadAssets()
{
    m_cubeMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::CubeData());
    m_tessellationMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::TessellatedQuadData());
    m_objMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::LoadObj(m_runtimeDirectory / L"assets/showcase.obj"));
    m_gatoMesh.Upload(m_device.Get(), m_commandList.Get(), Mesh::LoadObj(m_runtimeDirectory / L"assets/Gato.obj"));
    m_albedoTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/checker.ppm", CpuSrv(4));
    m_normalTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/normal.ppm", CpuSrv(5));
    m_displacementTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/displacement.ppm", CpuSrv(6));
    m_gatoTexture.LoadPpm(m_device.Get(), m_commandList.Get(), m_runtimeDirectory / L"assets/Gato.ppm", CpuSrv(7));
    m_device->CopyDescriptorsSimple(1, CpuSrv(8), CpuSrv(5), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CopyDescriptorsSimple(1, CpuSrv(9), CpuSrv(6), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void RenderingSystem::BuildScene()
{
    auto addObject = [&](XMFLOAT3 position, XMFLOAT3 scale, XMFLOAT4 color,
                         SceneMesh mesh = SceneMesh::Cube, float yaw = 0.0f,
                         XMFLOAT4 uvParameters = XMFLOAT4{ 2.0f, 2.0f, 0.06f, 0.025f })
    {
        SceneObject object;
        XMMATRIX world = XMMatrixScaling(scale.x, scale.y, scale.z) * XMMatrixRotationY(yaw) *
                         XMMatrixTranslation(position.x, position.y, position.z);
        XMStoreFloat4x4(&object.world, world);
        object.mesh = mesh;
        MeshFor(object).Bounds().Transform(object.bounds, world);
        object.color = color;
        object.uvParameters = uvParameters;
        object.textureTableStart = mesh == SceneMesh::Gato ? 7u : 4u;
        m_objects.push_back(object);
    };

    addObject({ 0,0,0 }, { 2,2,2 }, { .10f,.10f,.10f,1 });

    addObject({ 0,.0f,0 }, { 5,.5f,5 }, { .10f,.10f,.10f,1 });
    addObject({ 0,14.5f,30 }, { 28,.5f,80 }, { .30f,.32f,.36f,1 });
    addObject({ -14,7,30 }, { .5f,14,80 }, { .55f,.32f,.25f,1 });
    addObject({  14,7,30 }, { .5f,14,80 }, { .25f,.35f,.55f,1 });
    for (int z = 0; z < 14; ++z)
    {
        const float zz = static_cast<float>(z * 7);
        addObject({ -9,3.5f,zz }, { 1.0f,7.0f,1.0f }, { .7f,.65f,.52f,1 });
        addObject({  9,3.5f,zz }, { 1.0f,7.0f,1.0f }, { .7f,.65f,.52f,1 });
    }
    addObject({ 0,1.1f,8 }, { 2.2f,2.2f,2.2f }, { .8f,.45f,.18f,1 }, SceneMesh::Showcase);
    addObject({ 4.5f,-.13f,9 }, { .09f,.09f,.09f }, { 1,1,1,1 },
              SceneMesh::Gato, XM_PI, { 1,1,0.04f,0.02f });

    uint32_t state = 0x12345678u;
    auto random01 = [&]() { state = state * 1664525u + 1013904223u; return (state >> 8) * (1.0f / 16777216.0f); };
    for (int i = 0; i < 2048; ++i)
    {
        const float x = (random01() - .5f) * 220.0f;
        const float z = (random01() - .2f) * 220.0f;
        const float size = .25f + random01() * .65f;
        addObject({ x, size, z }, { size, size * 2.0f, size },
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

void RenderingSystem::UpdateVisibility(const Camera& camera)
{
    m_visibleIndices.clear();
    if (m_cullingMode == CullingMode::Disabled)
    {
        m_visibleIndices.resize(m_objects.size());
        for (uint32_t i = 0; i < m_objects.size(); ++i) m_visibleIndices[i] = i;
        return;
    }
    const BoundingFrustum frustum = camera.WorldFrustum();
    if (m_cullingMode == CullingMode::Octree)
    {
        m_octree->Query(frustum, m_visibleIndices);
        return;
    }
    for (uint32_t i = 0; i < m_objects.size(); ++i)
        if (frustum.Contains(m_objects[i].bounds) != DISJOINT) m_visibleIndices.push_back(i);
}

void RenderingSystem::UpdateShadowCascades(const Camera& camera)
{
    constexpr float splitLambda = 0.75f;
    constexpr float casterMargin = 120.0f;
    const float nearPlane = camera.NearPlane();
    const float farPlane = camera.FarPlane();
    const float tanHalfFov = std::tan(camera.FieldOfView() * 0.5f);
    const XMMATRIX inverseView = XMMatrixInverse(nullptr, camera.View());
    const XMVECTOR lightDirection = XMVector3Normalize(XMVectorSet(-0.25f, -1.0f, 0.2f, 0.0f));
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

        // Snap the cascade center in light-space X/Y. The projection then moves only
        // in whole shadow texels, eliminating most camera-motion shimmer.
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
        XMStoreFloat4x4(&m_shadowViewProjections[cascade], lightView * lightProjection);

        auto& visible = m_shadowVisibleIndices[cascade];
        visible.clear();
        visible.reserve(m_objects.size() / 2);
        for (uint32_t objectIndex = 0; objectIndex < m_objects.size(); ++objectIndex)
        {
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
            if (maximum.x >= -radius && minimum.x <= radius &&
                maximum.y >= -radius && minimum.y <= radius &&
                maximum.z >= 0.1f && minimum.z <= shadowFar)
                visible.push_back(objectIndex);
        }
        cascadeNear = cascadeFar;
    }
}

void RenderingSystem::RenderShadowMaps(const Camera& camera, float totalTime)
{
    auto toDepth = TransitionBarrier(m_shadowMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                     D3D12_RESOURCE_STATE_DEPTH_WRITE);
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
            const Mesh& mesh = MeshFor(object);
            mesh.Bind(m_commandList.Get());
            ShadowConstants constants{ object.world, m_shadowViewProjections[cascade] };
            m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
            for (const Submesh& submesh : mesh.Submeshes())
                m_commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        }

        m_commandList->SetGraphicsRootSignature(m_geometryRootSignature.Get());
        m_commandList->SetPipelineState(m_shadowTessellationPso.Get());
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
        m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(4));
        m_tessellationMesh.Bind(m_commandList.Get());
        ObjectConstants tessConstants{};
        XMStoreFloat4x4(&tessConstants.world, XMMatrixScaling(5, 1, 5) * XMMatrixTranslation(0, .15f, 17));
        tessConstants.viewProjection = m_shadowViewProjections[cascade];
        tessConstants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
        tessConstants.uvParameters = { 6.0f, 6.0f, 0.03f, 0.0f };
        tessConstants.materialColor = { 1,1,1,1 };
        tessConstants.tessellationParameters = { 12.0f, 1.0f, 3.0f, 45.0f };
        m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&tessConstants, sizeof(tessConstants)));
        m_commandList->DrawIndexedInstanced(m_tessellationMesh.IndexCount(), 1, 0, 0, 0);
    }

    auto toShaderResource = TransitionBarrier(m_shadowMap.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                              D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
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

void RenderingSystem::PopulateCommandList(const Camera& camera, float totalTime)
{
    ThrowIfFailed(m_allocators[m_frameIndex]->Reset(), "Reset command allocator");
    ThrowIfFailed(m_commandList->Reset(m_allocators[m_frameIndex].Get(), nullptr), "Reset command list");
    m_constantOffset = static_cast<UINT64>(m_frameIndex) * ConstantsPerFrame;
    m_commandList->RSSetViewports(1, &m_viewport);
    m_commandList->RSSetScissorRects(1, &m_scissor);
    ID3D12DescriptorHeap* heaps[]{ m_srvHeap.Get() };
    m_commandList->SetDescriptorHeaps(1, heaps);

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
        ObjectConstants constants{};
        constants.world = object.world;
        XMStoreFloat4x4(&constants.viewProjection, viewProjection);
        constants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
        constants.uvParameters = object.uvParameters;
        constants.tessellationParameters = { 1, 1, 1, 1 };
        const Mesh& mesh = MeshFor(object);
        m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(object.textureTableStart));
        mesh.Bind(m_commandList.Get());
        for (const Submesh& submesh : mesh.Submeshes())
        {
            const XMFLOAT4 material = submesh.materialIndex < mesh.Materials().size()
                ? mesh.Materials()[submesh.materialIndex].diffuse : XMFLOAT4{ 1,1,1,1 };
            constants.materialColor = {
                object.color.x * material.x, object.color.y * material.y,
                object.color.z * material.z, object.color.w * material.w
            };
            m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&constants, sizeof(constants)));
            m_commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        }
    }

    m_commandList->SetPipelineState(m_tessellationPso.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    m_tessellationMesh.Bind(m_commandList.Get());
    ObjectConstants tessConstants{};
    XMStoreFloat4x4(&tessConstants.world, XMMatrixScaling(5, 1, 5) * XMMatrixTranslation(0, .15f, 17));
    XMStoreFloat4x4(&tessConstants.viewProjection, viewProjection);
    tessConstants.cameraAndTime = { cameraPosition.x, cameraPosition.y, cameraPosition.z, totalTime };
    tessConstants.uvParameters = { 6.0f, 6.0f, 0.03f, 0.0f };
    tessConstants.materialColor = { .45f,.7f,.9f,1 };
    tessConstants.tessellationParameters = { 12.0f, 1.0f, 3.0f, 45.0f };
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&tessConstants, sizeof(tessConstants)));
    m_commandList->DrawIndexedInstanced(m_tessellationMesh.IndexCount(), 1, 0, 0, 0);
    m_gbuffer.EndGeometry(m_commandList.Get());

    auto backBarrier = TransitionBarrier(m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET);
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
    lighting.ambient = { .045f,.055f,.075f,1 };
    lighting.cascadeSplits = m_cascadeSplits;
    lighting.shadowParameters = { 1.0f / static_cast<float>(ShadowMapSize), 0.00035f, 0.0025f, 0.0f };
    const XMFLOAT3 cameraForward = camera.Direction();
    lighting.cameraForward = { cameraForward.x, cameraForward.y, cameraForward.z, 0.0f };
    lighting.lights[0] = { {0,0,0,0}, {-.25f,-1,.2f,0}, {1.0f,.92f,.78f,1.35f}, {}, {} };
    lighting.lights[1] = { {-6,5,8,13}, {0,0,0,1}, {1.0f,.15f,.08f,10}, {}, {} };
    lighting.lights[2] = { { 6,4,18,14}, {0,0,0,1}, {.08f,.25f,1.0f,11}, {}, {} };
    lighting.lights[3] = { {-5,3,31,12}, {0,0,0,1}, {.2f,1.0f,.35f,8}, {}, {} };
    lighting.lights[4] = { { 5,5,46,15}, {0,0,0,1}, {1.0f,.35f,.75f,10}, {}, {} };
    lighting.lights[5] = { { 0,8,5,35}, {0,-.75f,.65f,2}, {1.0f,.85f,.55f,16}, {.94f,.76f,0,0}, {} };
    lighting.lights[6] = { {-8,6,62,18}, {0,0,0,1}, {.2f,.65f,1.0f,10}, {}, {} };
    lighting.lights[7] = { { 8,4,78,16}, {0,0,0,1}, {1.0f,.55f,.15f,9}, {}, {} };
    m_commandList->SetGraphicsRootConstantBufferView(0, UploadConstants(&lighting, sizeof(lighting)));
    m_commandList->SetGraphicsRootDescriptorTable(1, GpuSrv(0));
    m_commandList->DrawInstanced(3, 1, 0, 0);

    backBarrier = TransitionBarrier(m_backBuffers[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_PRESENT);
    m_commandList->ResourceBarrier(1, &backBarrier);
    ThrowIfFailed(m_commandList->Close(), "Close frame command list");
}

void RenderingSystem::Render(const Camera& camera, float totalTime)
{
    m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
    WaitForFrame(m_frameIndex);
    UpdateVisibility(camera);
    UpdateShadowCascades(camera);
    PopulateCommandList(camera, totalTime);
    ID3D12CommandList* lists[]{ m_commandList.Get() };
    m_commandQueue->ExecuteCommandLists(1, lists);
    ThrowIfFailed(m_swapChain->Present(1, 0), "Present");
    const UINT64 value = ++m_nextFenceValue;
    ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), value), "Signal frame fence");
    m_frameFenceValues[m_frameIndex] = value;
}

void RenderingSystem::WaitForFrame(UINT frameIndex)
{
    const UINT64 value = m_frameFenceValues[frameIndex];
    if (value && m_fence->GetCompletedValue() < value)
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
    auto handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_srvIncrement;
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderingSystem::GpuSrv(UINT index) const
{
    auto handle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(index) * m_srvIncrement;
    return handle;
}
