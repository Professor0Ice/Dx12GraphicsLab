#pragma once
#include "Camera.hpp"
#include "GBuffer.hpp"
#include "Mesh.hpp"
#include "Octree.hpp"
#include "Texture.hpp"

enum class CullingMode
{
    Disabled,
    Frustum,
    Octree
};

class RenderingSystem
{
public:
    RenderingSystem(HWND window, UINT width, UINT height);
    ~RenderingSystem();
    RenderingSystem(const RenderingSystem&) = delete;
    RenderingSystem& operator=(const RenderingSystem&) = delete;

    void Render(const Camera& camera, float totalTime);
    void SetCullingMode(CullingMode mode) { m_cullingMode = mode; }
    void ToggleOctreeCulling()
    {
        m_cullingMode = m_cullingMode == CullingMode::Octree
            ? CullingMode::Disabled : CullingMode::Octree;
    }
    bool IsOctreeCullingEnabled() const { return m_cullingMode == CullingMode::Octree; }
    CullingMode GetCullingMode() const { return m_cullingMode; }
    size_t VisibleObjectCount() const { return m_visibleIndices.size(); }
    size_t TotalObjectCount() const { return m_objects.size(); }

private:
    static constexpr UINT FrameCount = 2;
    static constexpr UINT MaxLights = 16;
    static constexpr UINT CascadeCount = 4;
    static constexpr UINT ShadowMapSize = 2048;
    static constexpr UINT64 ConstantsPerFrame = 4 * 1024 * 1024;

    enum class SceneMesh
    {
        Cube,
        Showcase,
        Gato
    };

    struct SceneObject
    {
        DirectX::XMFLOAT4X4 world{};
        DirectX::BoundingBox bounds{};
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1 };
        DirectX::XMFLOAT4 uvParameters{ 2.0f, 2.0f, 0.06f, 0.025f };
        SceneMesh mesh = SceneMesh::Cube;
        UINT textureTableStart = 4;
    };

    struct alignas(16) ObjectConstants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4 cameraAndTime;
        DirectX::XMFLOAT4 uvParameters;
        DirectX::XMFLOAT4 materialColor;
        DirectX::XMFLOAT4 tessellationParameters;
    };

    struct alignas(16) LightGpu
    {
        DirectX::XMFLOAT4 positionAndRange;
        DirectX::XMFLOAT4 directionAndType;
        DirectX::XMFLOAT4 colorAndIntensity;
        DirectX::XMFLOAT4 spotAngles;
        DirectX::XMFLOAT4 padding;
    };

    struct alignas(16) LightingConstants
    {
        DirectX::XMFLOAT4X4 inverseViewProjection;
        std::array<DirectX::XMFLOAT4X4, CascadeCount> shadowViewProjections;
        DirectX::XMFLOAT4 cameraAndLightCount;
        DirectX::XMFLOAT4 ambient;
        DirectX::XMFLOAT4 cascadeSplits;
        DirectX::XMFLOAT4 shadowParameters;
        DirectX::XMFLOAT4 cameraForward;
        std::array<LightGpu, MaxLights> lights;
    };

    struct alignas(16) ShadowConstants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4X4 lightViewProjection;
    };

    void CreateDeviceAndSwapChain(HWND window);
    void CreateDescriptors();
    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateShadowResources();
    void CreateConstantUpload();
    void LoadAssets();
    void BuildScene();
    const Mesh& MeshFor(const SceneObject& object) const;
    void UpdateVisibility(const Camera& camera);
    void UpdateShadowCascades(const Camera& camera);
    void RenderShadowMaps(const Camera& camera, float totalTime);
    void PopulateCommandList(const Camera& camera, float totalTime);
    D3D12_GPU_VIRTUAL_ADDRESS UploadConstants(const void* data, size_t size);
    ComPtr<ID3DBlob> CompileShader(const std::filesystem::path& file,
                                  const char* entry, const char* target) const;
    void WaitForFrame(UINT frameIndex);
    void FlushGpu();
    D3D12_CPU_DESCRIPTOR_HANDLE CpuSrv(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GpuSrv(UINT index) const;

    UINT m_width = 0;
    UINT m_height = 0;
    std::filesystem::path m_runtimeDirectory;
    D3D12_VIEWPORT m_viewport{};
    D3D12_RECT m_scissor{};

    ComPtr<IDXGIFactory6> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<IDXGISwapChain3> m_swapChain;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> m_allocators;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    ComPtr<ID3D12DescriptorHeap> m_swapChainRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    std::array<ComPtr<ID3D12Resource>, FrameCount> m_backBuffers;
    UINT m_rtvIncrement = 0;
    UINT m_srvIncrement = 0;

    ComPtr<ID3D12RootSignature> m_geometryRootSignature;
    ComPtr<ID3D12RootSignature> m_lightingRootSignature;
    ComPtr<ID3D12RootSignature> m_shadowRootSignature;
    ComPtr<ID3D12PipelineState> m_geometryPso;
    ComPtr<ID3D12PipelineState> m_tessellationPso;
    ComPtr<ID3D12PipelineState> m_lightingPso;
    ComPtr<ID3D12PipelineState> m_shadowPso;
    ComPtr<ID3D12PipelineState> m_shadowTessellationPso;

    ComPtr<ID3D12Resource> m_shadowMap;
    ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissor{};
    std::array<DirectX::XMFLOAT4X4, CascadeCount> m_shadowViewProjections{};
    std::array<std::vector<uint32_t>, CascadeCount> m_shadowVisibleIndices;
    DirectX::XMFLOAT4 m_cascadeSplits{};

    ComPtr<ID3D12Resource> m_constantUpload;
    uint8_t* m_constantMapped = nullptr;
    UINT64 m_constantOffset = 0;
    UINT m_frameIndex = 0;

    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_nextFenceValue = 0;
    std::array<UINT64, FrameCount> m_frameFenceValues{};

    GBuffer m_gbuffer;
    Mesh m_cubeMesh;
    Mesh m_objMesh;
    Mesh m_gatoMesh;
    Mesh m_tessellationMesh;
    Texture m_albedoTexture;
    Texture m_normalTexture;
    Texture m_displacementTexture;
    Texture m_gatoTexture;
    std::vector<SceneObject> m_objects;
    std::vector<SceneBounds> m_sceneBounds;
    std::vector<uint32_t> m_visibleIndices;
    std::unique_ptr<Octree> m_octree;
    CullingMode m_cullingMode = CullingMode::Octree;
};
