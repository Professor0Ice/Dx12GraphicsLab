#pragma once
#include "Common.hpp"

struct Vertex
{
    DirectX::XMFLOAT3 position; // Локальная позиция вершины.
    DirectX::XMFLOAT3 normal; // Направление поверхности для света.
    DirectX::XMFLOAT4 tangent; // Ось tangent space для normal map.
    DirectX::XMFLOAT2 uv; // Координаты выборки текстуры.
};

struct MaterialDescription
{
    std::string name = "default";
    DirectX::XMFLOAT4 diffuse{ 1, 1, 1, 1 };
    std::filesystem::path albedoTexture;
    std::filesystem::path normalTexture;
    std::filesystem::path displacementTexture;
};

struct Submesh
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct MeshData
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Submesh> submeshes;
    std::vector<MaterialDescription> materials;
};

class Mesh
{
public:
    static MeshData CubeData();
    static MeshData BillboardData();
    static MeshData TessellatedQuadData();
    static MeshData LoadObj(const std::filesystem::path& path);

    void Upload(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const MeshData& data);
    void Bind(ID3D12GraphicsCommandList* commandList) const;
    uint32_t IndexCount() const { return m_indexCount; }
    const std::vector<Submesh>& Submeshes() const { return m_submeshes; }
    const std::vector<MaterialDescription>& Materials() const { return m_materials; }
    const DirectX::BoundingBox& Bounds() const { return m_bounds; }

private:
    ComPtr<ID3D12Resource> m_vertexBuffer; // Вершины
    ComPtr<ID3D12Resource> m_indexBuffer; // Индексы
    ComPtr<ID3D12Resource> m_vertexUpload; // Временная CPU-доступная память загрузки вершин.
    ComPtr<ID3D12Resource> m_indexUpload; // Временная CPU-доступная память загрузки индексов.
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{}; // Адрес, размер и шаг одной вершины
    D3D12_INDEX_BUFFER_VIEW m_indexView{}; // Адрес, размер и формат индексов
    uint32_t m_indexCount = 0;
    DirectX::BoundingBox m_bounds{};
    std::vector<Submesh> m_submeshes;
    std::vector<MaterialDescription> m_materials;
};
