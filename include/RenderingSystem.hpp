#pragma once
#include "Camera.hpp"
#include "GBuffer.hpp"
#include "Mesh.hpp"
#include "Octree.hpp"
#include "Texture.hpp"

enum class PostProcessMode : uint32_t
{
    None = 0,
    Grayscale = 1,
    SobelEdges = 2,
    ShadowCascades = 3
};

class RenderingSystem
{
public:
    RenderingSystem(HWND window, UINT width, UINT height);
    ~RenderingSystem();
    RenderingSystem(const RenderingSystem&) = delete;
    RenderingSystem& operator=(const RenderingSystem&) = delete;

    void Render(const Camera& camera, float totalTime, float deltaTime);
    void SetPostProcessMode(PostProcessMode mode) { m_postProcessMode = mode; }
    PostProcessMode GetPostProcessMode() const { return m_postProcessMode; }
    size_t VisibleObjectCount() const { return m_visibleIndices.size(); }
    size_t TotalObjectCount() const { return m_objects.size(); }

private:
    static constexpr UINT FrameCount = 2; // Два back buffer чтобы CPU и GPU синхранизация
    static constexpr UINT MaxLights = 16; // Должен совпадать с gLights в PostProcess.hlsl
    static constexpr UINT CascadeCount = 4;
    static constexpr UINT ShadowMapSize = 2048;
    static constexpr UINT64 ConstantsPerFrame = 4 * 1024 * 1024; // Отдельный участок upload-буфера на кадр.
    static constexpr UINT CachedVertexStride = 11 * sizeof(float);
    static constexpr UINT64 CachedVertexBufferBytes = 6ull * 64 * 64 * CachedVertexStride;
    static constexpr UINT MaxParticles = 4096;

    enum class SceneMesh
    {
        Cube,
        Showcase,
        Gato
    };

    enum class LodLevel : uint8_t
    {
        Mesh = 0,
        Sprite = 1,
        Hidden = 2,
        FarHidden = 3
    };

    struct SceneObject
    {
        DirectX::XMFLOAT4X4 world{}; // Переводит вершины модели в мировое пространство.
        DirectX::XMFLOAT4X4 spriteWorld{};
        DirectX::BoundingBox bounds{}; // Границы объекта для отсечения невидимых объектов.
        DirectX::XMFLOAT4 color{ 1, 1, 1, 1 }; // Множитель цвета материала RGBA.
        DirectX::XMFLOAT4 materialParameters{ 0.0f, 0.65f, 1.0f, 0.0f }; // metallic, roughness, AO.
        DirectX::XMFLOAT4 uvParameters{ 2.0f, 2.0f, 0.06f, 0.025f }; // Масштаб 
        SceneMesh mesh = SceneMesh::Cube;
        LodLevel lod = LodLevel::Mesh;
        UINT textureTableStart = 4; // Первый SRV текстур объекта в общей таблице дескрипторов.
    };

    // Данные одного объекта в точности повторяют cbuffer ObjectConstants в HLSL.
    // Выравнивание 16 нужно для корректной раскладки float4 и матриц на стороне GPU.
    struct alignas(16) ObjectConstants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4 cameraAndTime;
        DirectX::XMFLOAT4 uvParameters;
        DirectX::XMFLOAT4 materialColor;
        DirectX::XMFLOAT4 materialParameters;
        DirectX::XMFLOAT4 tessellationParameters;
    };

    // Представление одного света для GPU. Компонента w хранит дальность, тип или интенсивность.
    struct alignas(16) LightGpu
    {
        DirectX::XMFLOAT4 positionAndRange;
        DirectX::XMFLOAT4 directionAndType;
        DirectX::XMFLOAT4 colorAndIntensity;
        DirectX::XMFLOAT4 spotAngles;
        DirectX::XMFLOAT4 padding;
    };

    // Общие данные отложенного освещения, загружаемые один раз на кадр.
    struct alignas(16) LightingConstants
    {
        DirectX::XMFLOAT4X4 inverseViewProjection;
        std::array<DirectX::XMFLOAT4X4, CascadeCount> shadowViewProjections;
        DirectX::XMFLOAT4 cameraAndLightCount;
        DirectX::XMFLOAT4 iblParameters; // max prefiltered mip, IBL intensity, exposure.
        DirectX::XMFLOAT4 cascadeSplits;
        DirectX::XMFLOAT4 shadowParameters;
        DirectX::XMFLOAT4 cameraForward;
        DirectX::XMFLOAT4 postProcessParameters;
        std::array<LightGpu, MaxLights> lights;
    };

    // Минимальный набор матриц для прохода, записывающего глубину от лица солнца.
    struct alignas(16) ShadowConstants
    {
        DirectX::XMFLOAT4X4 world;
        DirectX::XMFLOAT4X4 lightViewProjection;
    };

    // Должна совпадать с Particle в ParticleUpdate.hlsl и ParticleRender.hlsl.
    struct alignas(16) ParticleGpu
    {
        DirectX::XMFLOAT3 position;
        float padding0;
        DirectX::XMFLOAT3 velocity;
        float padding1;
    };

    struct alignas(16) ParticleSimulationConstants
    {
        float deltaTime;
        float totalTime;
        UINT particleCount;
        float padding;
        DirectX::XMFLOAT3 emitterPosition;
        float fallSpeed;
    };

    struct alignas(16) ParticleDrawConstants
    {
        DirectX::XMFLOAT4X4 viewProjection;
        DirectX::XMFLOAT4 cameraRight;
        DirectX::XMFLOAT4 cameraUp;
        DirectX::XMFLOAT4 colorAndSize;
        DirectX::XMFLOAT4 parameters; // x = opacity.
    };

    struct ParticleSortItem
    {
        float distanceSquared;
        UINT particleIndex;
    };

    struct alignas(16) ParticleSortConstants
    {
        DirectX::XMFLOAT3 cameraPosition;
        UINT particleCount;
        UINT stageK;
        UINT stageJ;
        DirectX::XMFLOAT2 padding;
    };

    void CreateDeviceAndSwapChain(HWND window);
    void CreateDescriptors();
    void CreateRootSignatures();
    void CreatePipelineStates();
    void CreateShadowResources();
    void CreateConstantUpload();
    void CreateTessellationCache();
    void CreateParticleResources();
    void LoadAssets();
    void BuildScene();
    const Mesh& MeshFor(const SceneObject& object) const;
    void UpdateLods(const Camera& camera);
    void UpdateVisibility(const Camera& camera);
    void UpdateShadowCascades(const Camera& camera);
    void RenderShadowMaps(const Camera& camera, float totalTime);
    void PopulateCommandList(const Camera& camera, float totalTime, float deltaTime,
                             bool refreshTessellation);
    void UpdateTessellationCache(const Camera& camera, float totalTime);
    void DrawCachedTessellation(const ObjectConstants& constants);
    void UpdateParticles(float deltaTime);
    void UpdateReverseParticles(float deltaTime);
    void SortReverseParticles(const Camera& camera);
    void DrawParticles(const Camera& camera);
    void DrawTransparentParticles(const Camera& camera);
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
    D3D12_VIEWPORT m_viewport{}; // Преобразует координаты после шейдера в пиксели окна.
    D3D12_RECT m_scissor{}; // Запрещает растеризацию вне клиентской области окна.

    ComPtr<IDXGIFactory6> m_factory; // DXGI перечисляет адаптеры и создаёт swap chain.
    ComPtr<ID3D12Device> m_device; // Главный объект Direct3D 12 для создания GPU-ресурсов.
    ComPtr<ID3D12CommandQueue> m_commandQueue; // Очередь выполнения команд на GPU.
    ComPtr<IDXGISwapChain3> m_swapChain; // Набор кадров, по очереди показываемых в окне.
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> m_allocators; // Память команд отдельно для каждого кадра.
    ComPtr<ID3D12GraphicsCommandList> m_commandList; // Список команд текущего кадра.
    ComPtr<ID3D12DescriptorHeap> m_swapChainRtvHeap;
    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    std::array<ComPtr<ID3D12Resource>, FrameCount> m_backBuffers;
    UINT m_rtvIncrement = 0;
    UINT m_srvIncrement = 0;

    ComPtr<ID3D12RootSignature> m_geometryRootSignature; // Связи констант и текстур геометрического прохода.
    ComPtr<ID3D12RootSignature> m_lightingRootSignature; // Связи G-buffer, теней и света.
    ComPtr<ID3D12RootSignature> m_shadowRootSignature; // Только матрицы объекта и солнца для теней.
    ComPtr<ID3D12PipelineState> m_geometryPso;
    ComPtr<ID3D12PipelineState> m_spritePso;
    ComPtr<ID3D12PipelineState> m_lightingPso;
    ComPtr<ID3D12PipelineState> m_shadowPso;
    ComPtr<ID3D12PipelineState> m_spriteShadowPso;
    ComPtr<ID3D12PipelineState> m_tessCapturePso;
    ComPtr<ID3D12PipelineState> m_cachedTessellationPso;
    ComPtr<ID3D12PipelineState> m_cachedShadowPso;
    ComPtr<ID3D12PipelineState> m_tessDrawArgsPso;
    ComPtr<ID3D12RootSignature> m_tessDrawArgsRootSignature;
    ComPtr<ID3D12CommandSignature> m_tessDrawCommandSignature;
    ComPtr<ID3D12RootSignature> m_particleComputeRootSignature;
    ComPtr<ID3D12RootSignature> m_particleDrawRootSignature;
    ComPtr<ID3D12RootSignature> m_particleSortRootSignature;
    ComPtr<ID3D12RootSignature> m_transparentParticleDrawRootSignature;
    ComPtr<ID3D12PipelineState> m_particleInitializePso;
    ComPtr<ID3D12PipelineState> m_particleComputePso;
    ComPtr<ID3D12PipelineState> m_particleDrawPso;
    ComPtr<ID3D12PipelineState> m_reverseParticleInitializePso;
    ComPtr<ID3D12PipelineState> m_reverseParticleComputePso;
    ComPtr<ID3D12PipelineState> m_particleSortBuildPso;
    ComPtr<ID3D12PipelineState> m_particleSortStepPso;
    ComPtr<ID3D12PipelineState> m_transparentParticleDrawPso;

    ComPtr<ID3D12Resource> m_shadowMap; 
    ComPtr<ID3D12DescriptorHeap> m_shadowDsvHeap;//на каждый каскад для записи глубины
    D3D12_VIEWPORT m_shadowViewport{};
    D3D12_RECT m_shadowScissor{};
    std::array<DirectX::XMFLOAT4X4, CascadeCount> m_shadowViewProjections{};
    std::array<std::vector<uint32_t>, CascadeCount> m_shadowVisibleIndices;
    DirectX::XMFLOAT4 m_cascadeSplits{};

    ComPtr<ID3D12Resource> m_constantUpload; 
    uint8_t* m_constantMapped = nullptr;
    UINT64 m_constantOffset = 0; 
    UINT m_frameIndex = 0;

    DirectX::XMFLOAT3 m_cachedTessCameraPosition{};
    bool m_tessCacheValid = false;
    ComPtr<ID3D12Resource> m_cachedTessVertices;
    ComPtr<ID3D12Resource> m_cachedTessFilledSize;
    ComPtr<ID3D12Resource> m_tessFilledSizeReset;
    ComPtr<ID3D12Resource> m_cachedTessDrawArgs;
    D3D12_VERTEX_BUFFER_VIEW m_cachedTessVertexView{};

    std::array<ComPtr<ID3D12Resource>, 2> m_particleBuffers;
    std::array<ComPtr<ID3D12Resource>, 2> m_particleCounters;
    std::array<D3D12_RESOURCE_STATES, 2> m_particleBufferStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    };
    ComPtr<ID3D12Resource> m_particleCounterUpload;
    UINT m_particleReadBuffer = 0;
    std::array<ComPtr<ID3D12Resource>, 2> m_reverseParticleBuffers;
    std::array<ComPtr<ID3D12Resource>, 2> m_reverseParticleCounters;
    std::array<D3D12_RESOURCE_STATES, 2> m_reverseParticleBufferStates{
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS
    };
    UINT m_reverseParticleReadBuffer = 0;
    ComPtr<ID3D12Resource> m_reverseParticleSortItems;
    D3D12_RESOURCE_STATES m_reverseParticleSortState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    float m_particleTime = 0.0f;

    ComPtr<ID3D12Fence> m_fence; 
    HANDLE m_fenceEvent = nullptr;
    UINT64 m_nextFenceValue = 0;
    std::array<UINT64, FrameCount> m_frameFenceValues{};

    GBuffer m_gbuffer;
    Mesh m_cubeMesh;
    Mesh m_billboardMesh;
    Mesh m_objMesh;
    Mesh m_gatoMesh;
    Mesh m_tessellationMesh;
    Texture m_albedoTexture;
    Texture m_normalTexture;
    Texture m_displacementTexture;
    Texture m_gatoTexture;
    Texture m_irradianceMap;
    Texture m_brdfIntegrationMap;
    Texture m_prefilteredEnvironmentMap;
    std::vector<SceneObject> m_objects;
    std::vector<SceneBounds> m_sceneBounds;
    std::vector<uint32_t> m_visibleIndices;
    std::unique_ptr<Octree> m_octree;
    PostProcessMode m_postProcessMode = PostProcessMode::ShadowCascades;
};
