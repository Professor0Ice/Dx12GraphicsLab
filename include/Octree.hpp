#pragma once
#include "Common.hpp"

struct SceneBounds
{
    DirectX::BoundingBox box;
    uint32_t objectIndex = 0;
};

class Octree
{
public:
    Octree(DirectX::BoundingBox worldBounds, uint32_t maxDepth = 7, uint32_t capacity = 16);
    void Build(const std::vector<SceneBounds>& objects);
    void Query(const DirectX::BoundingFrustum& frustum, std::vector<uint32_t>& visible) const;

private:
    struct Node
    {
        DirectX::BoundingBox bounds;
        std::vector<uint32_t> items;
        std::array<std::unique_ptr<Node>, 8> children;
        bool IsLeaf() const { return children[0] == nullptr; }
    };

    void Insert(Node& node, uint32_t boundsIndex, uint32_t depth);
    void Subdivide(Node& node);
    void QueryNode(const Node& node, const DirectX::BoundingFrustum& frustum,
                   std::vector<uint32_t>& visible) const;
    int ContainingChild(const Node& node, const DirectX::BoundingBox& box) const;

    std::unique_ptr<Node> m_root;
    std::vector<SceneBounds> m_objects;
    uint32_t m_maxDepth;
    uint32_t m_capacity;
};

