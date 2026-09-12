#pragma once
#include "Common.hpp"

struct Vertex
{
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 tangent;
    DirectX::XMFLOAT2 uv;
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
    static MeshData TessellatedQuadData();
    static MeshData LoadObj(const std::filesystem::path& path);

    void Upload(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const MeshData& data);
    void Bind(ID3D12GraphicsCommandList* commandList) const;
    uint32_t IndexCount() const { return m_indexCount; }
    const std::vector<Submesh>& Submeshes() const { return m_submeshes; }
    const std::vector<MaterialDescription>& Materials() const { return m_materials; }
    const DirectX::BoundingBox& Bounds() const { return m_bounds; }

private:
    ComPtr<ID3D12Resource> m_vertexBuffer;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_vertexUpload;
    ComPtr<ID3D12Resource> m_indexUpload;
    D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
    D3D12_INDEX_BUFFER_VIEW m_indexView{};
    uint32_t m_indexCount = 0;
    DirectX::BoundingBox m_bounds{};
    std::vector<Submesh> m_submeshes;
    std::vector<MaterialDescription> m_materials;
};
