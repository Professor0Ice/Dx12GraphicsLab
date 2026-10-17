#include "Terrain.hpp"

using namespace DirectX;

TerrainQuadtree::TerrainQuadtree(XMFLOAT2 center, float size, float minimumHeight, float maximumHeight, uint32_t maxDepth) : m_minimumHeight(minimumHeight), m_maximumHeight(maximumHeight), m_maxDepth(maxDepth)
{
    m_root = BuildNode(center, size, 0);
}

std::unique_ptr<TerrainQuadtree::Node> TerrainQuadtree::BuildNode(XMFLOAT2 center, float size, uint32_t depth)
{
    auto node = std::make_unique<Node>();
    node->center = center;
    node->size = size;
    node->depth = depth;
    node->bounds.Center = { center.x, (m_minimumHeight + m_maximumHeight) * 0.5f, center.y };
    node->bounds.Extents = { size * 0.5f, (m_maximumHeight - m_minimumHeight) * 0.5f, size * 0.5f };

    if (depth < m_maxDepth)
    {
        const float childSize = size * 0.5f;
        const float offset = size * 0.25f;
        node->children[0] = BuildNode({ center.x - offset, center.y - offset }, childSize, depth + 1);
        node->children[1] = BuildNode({ center.x + offset, center.y - offset }, childSize, depth + 1);
        node->children[2] = BuildNode({ center.x - offset, center.y + offset }, childSize, depth + 1);
        node->children[3] = BuildNode({ center.x + offset, center.y + offset }, childSize, depth + 1);
    }
    return node;
}

void TerrainQuadtree::Select(const BoundingFrustum& frustum, XMFLOAT3 cameraPosition, std::vector<TerrainPatch>& visiblePatches) const
{
    visiblePatches.clear();
    SelectNode(*m_root, frustum, cameraPosition, visiblePatches);
}

void TerrainQuadtree::SelectNode(const Node& node, const BoundingFrustum& frustum, XMFLOAT3 cameraPosition, std::vector<TerrainPatch>& visiblePatches) const
{

    if (frustum.Contains(node.bounds) == DISJOINT)
        return;

    const float halfSize = node.size * 0.5f;
    const float dx = std::max(std::abs(cameraPosition.x - node.center.x) - halfSize, 0.0f);
    const float dz = std::max(std::abs(cameraPosition.z - node.center.y) - halfSize, 0.0f);
    const float distanceToPatch = std::sqrt(dx * dx + dz * dz);

    const bool split = node.depth < m_maxDepth && distanceToPatch < node.size * 1.35f;
    if (split)
    {
        for (const auto& child : node.children)
            SelectNode(*child, frustum, cameraPosition, visiblePatches);
        return;
    }

    visiblePatches.push_back({ node.center, node.size, node.depth, node.bounds });
}
