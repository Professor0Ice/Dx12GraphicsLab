#pragma once
#include "Common.hpp"

struct TerrainPatch
{
    DirectX::XMFLOAT2 center{};
    float size = 0.0f;
    uint32_t lod = 0;
    DirectX::BoundingBox bounds{};
};

class TerrainQuadtree
{
public:
    TerrainQuadtree(DirectX::XMFLOAT2 center, float size, float minimumHeight, float maximumHeight, uint32_t maxDepth);

    void Select(const DirectX::BoundingFrustum& frustum, DirectX::XMFLOAT3 cameraPosition,std::vector<TerrainPatch>& visiblePatches) const;

private:
    struct Node
    {
        DirectX::XMFLOAT2 center{};
        float size = 0.0f;
        uint32_t depth = 0;
        DirectX::BoundingBox bounds{};
        std::array<std::unique_ptr<Node>, 4> children;
    };

    std::unique_ptr<Node> BuildNode(DirectX::XMFLOAT2 center, float size, uint32_t depth);
    void SelectNode(const Node& node, const DirectX::BoundingFrustum& frustum, DirectX::XMFLOAT3 cameraPosition,
                    std::vector<TerrainPatch>& visiblePatches) const;

    float m_minimumHeight = 0.0f;
    float m_maximumHeight = 0.0f;
    uint32_t m_maxDepth = 0;
    std::unique_ptr<Node> m_root;
};
