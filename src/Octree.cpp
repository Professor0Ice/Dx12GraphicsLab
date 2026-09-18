#include "Octree.hpp"

using namespace DirectX;

Octree::Octree(BoundingBox worldBounds, uint32_t maxDepth, uint32_t capacity)
    : m_root(std::make_unique<Node>()), m_maxDepth(maxDepth), m_capacity(capacity)
{
    m_root->bounds = worldBounds;
}

void Octree::Build(const std::vector<SceneBounds>& objects)
{
    const BoundingBox bounds = m_root->bounds;
    m_root = std::make_unique<Node>();
    m_root->bounds = bounds;
    m_objects = objects;
    for (uint32_t i = 0; i < m_objects.size(); ++i)
        Insert(*m_root, i, 0);
}

int Octree::ContainingChild(const Node& node, const BoundingBox& box) const
{
    const XMFLOAT3 center = node.bounds.Center;
    const XMFLOAT3 minPoint{ box.Center.x - box.Extents.x, box.Center.y - box.Extents.y, box.Center.z - box.Extents.z };
    const XMFLOAT3 maxPoint{ box.Center.x + box.Extents.x, box.Center.y + box.Extents.y, box.Center.z + box.Extents.z };
    const int x = maxPoint.x <= center.x ? 0 : (minPoint.x >= center.x ? 1 : -1);
    const int y = maxPoint.y <= center.y ? 0 : (minPoint.y >= center.y ? 1 : -1);
    const int z = maxPoint.z <= center.z ? 0 : (minPoint.z >= center.z ? 1 : -1);
    return (x < 0 or y < 0 or z < 0) ? -1 : x | (y << 1) | (z << 2);
}

void Octree::Subdivide(Node& node)
{
    const XMFLOAT3 childExtents{
        node.bounds.Extents.x * 0.5f,
        node.bounds.Extents.y * 0.5f,
        node.bounds.Extents.z * 0.5f
    };
    for (int i = 0; i < 8; ++i)
    {
        node.children[i] = std::make_unique<Node>();
        node.children[i]->bounds.Extents = childExtents;
        node.children[i]->bounds.Center = {
            node.bounds.Center.x + ((i & 1) ? childExtents.x : -childExtents.x),
            node.bounds.Center.y + ((i & 2) ? childExtents.y : -childExtents.y),
            node.bounds.Center.z + ((i & 4) ? childExtents.z : -childExtents.z)
        };
    }
}

void Octree::Insert(Node& node, uint32_t boundsIndex, uint32_t depth)
{
    if (not node.IsLeaf())
    {
        const int child = ContainingChild(node, m_objects[boundsIndex].box);
        if (child >= 0)
        {
            Insert(*node.children[child], boundsIndex, depth + 1);
            return;
        }
    }

    node.items.push_back(boundsIndex);
    if (node.IsLeaf() and node.items.size() > m_capacity and depth < m_maxDepth)
    {
        Subdivide(node);
        std::vector<uint32_t> retained;
        for (uint32_t item : node.items)
        {
            const int child = ContainingChild(node, m_objects[item].box);
            if (child >= 0)
                Insert(*node.children[child], item, depth + 1);
            else
                retained.push_back(item);
        }
        node.items = std::move(retained);
    }
}

void Octree::Query(const BoundingFrustum& frustum, std::vector<uint32_t>& visible) const
{
    visible.clear();
    QueryNode(*m_root, frustum, visible);
}

void Octree::QueryNode(const Node& node, const BoundingFrustum& frustum,std::vector<uint32_t>& visible) const
{
    if (frustum.Contains(node.bounds) == DISJOINT)
        return;

    for (uint32_t item : node.items)
    {
        // Объект добавляется, если его bounding box хотя бы частично попадает во frustum.
        if (frustum.Contains(m_objects[item].box) not_eq DISJOINT)
            visible.push_back(m_objects[item].objectIndex);
    }
    if (not node.IsLeaf())
    {
        for (const auto& child : node.children)
            QueryNode(*child, frustum, visible);
    }
}
