#include "Mesh.hpp"

using namespace DirectX;

namespace
{
    struct ObjKey
    {
        int p = 0, t = 0, n = 0;
        bool operator==(const ObjKey& other) const { return p == other.p and t == other.t and n == other.n; }
    };

    struct ObjKeyHash
    {
        size_t operator()(const ObjKey& key) const
        {
            return (static_cast<size_t>(key.p) * 73856093u) ^
                   (static_cast<size_t>(key.t) * 19349663u) ^
                   (static_cast<size_t>(key.n) * 83492791u);
        }
    };

    int ResolveIndex(int index, size_t count)
    {
        return index > 0 ? index - 1 : static_cast<int>(count) + index;
    }

    ObjKey ParseFaceIndex(const std::string& token)
    {
        ObjKey result{};
        std::stringstream stream(token);
        std::string part;
        if (std::getline(stream, part, '/') and not part.empty()) result.p = std::stoi(part);
        if (std::getline(stream, part, '/') and not part.empty()) result.t = std::stoi(part);
        if (std::getline(stream, part, '/') and not part.empty()) result.n = std::stoi(part);
        return result;
    }

    void CalculateTangents(MeshData& data)
    {
        std::vector<XMFLOAT3> tangentSums(data.vertices.size(), XMFLOAT3{});
        for (size_t i = 0; i + 2 < data.indices.size(); i += 3)
        {
            Vertex& a = data.vertices[data.indices[i]];
            Vertex& b = data.vertices[data.indices[i + 1]];
            Vertex& c = data.vertices[data.indices[i + 2]];
            const XMFLOAT3 e1{ b.position.x - a.position.x, b.position.y - a.position.y, b.position.z - a.position.z };
            const XMFLOAT3 e2{ c.position.x - a.position.x, c.position.y - a.position.y, c.position.z - a.position.z };
            const float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y;
            const float du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
            const float determinant = du1 * dv2 - du2 * dv1;
            if (std::abs(determinant) < 1e-7f) continue;
            const float inv = 1.0f / determinant;
            const XMFLOAT3 tangent{
                (e1.x * dv2 - e2.x * dv1) * inv,
                (e1.y * dv2 - e2.y * dv1) * inv,
                (e1.z * dv2 - e2.z * dv1) * inv
            };
            for (int j = 0; j < 3; ++j)
            {
                auto& sum = tangentSums[data.indices[i + j]];
                sum.x += tangent.x; sum.y += tangent.y; sum.z += tangent.z;
            }
        }
        for (size_t i = 0; i < data.vertices.size(); ++i)
        {
            XMVECTOR tangent = XMVector3Normalize(XMLoadFloat3(&tangentSums[i]));
            XMFLOAT3 value{};
            XMStoreFloat3(&value, tangent);
            data.vertices[i].tangent = { value.x, value.y, value.z, 1.0f };
        }
    }

    std::vector<MaterialDescription> LoadMaterials(const std::filesystem::path& path)
    {
        std::vector<MaterialDescription> materials;
        std::ifstream file(path);
        if (not file) return materials;
        MaterialDescription* current = nullptr;
        std::string line;
        while (std::getline(file, line))
        {
            std::stringstream stream(line);
            std::string command;
            stream >> command;
            if (command == "newmtl")
            {
                materials.emplace_back();
                current = &materials.back();
                stream >> current->name;
            }
            else if (current and command == "Kd")
            {
                stream >> current->diffuse.x >> current->diffuse.y >> current->diffuse.z;
            }
            else if (current and command == "map_Kd")
            {
                std::string value; stream >> value; current->albedoTexture = path.parent_path() / value;
            }
            else if (current and (command == "map_Bump" or command == "bump"))
            {
                std::string value; stream >> value; current->normalTexture = path.parent_path() / value;
            }
            else if (current and command == "disp")
            {
                std::string value; stream >> value; current->displacementTexture = path.parent_path() / value;
            }
        }
        return materials;
    }
}

MeshData Mesh::CubeData()
{
    MeshData data;
    const XMFLOAT4 tX{ 1, 0, 0, 1 }, tNX{ -1, 0, 0, 1 }, tZ{ 0, 0, 1, 1 }, tNZ{ 0, 0, -1, 1 };
    const auto face = [&](XMFLOAT3 a, XMFLOAT3 b, XMFLOAT3 c, XMFLOAT3 d, XMFLOAT3 n, XMFLOAT4 tangent)
    {
        const uint32_t start = static_cast<uint32_t>(data.vertices.size());
        data.vertices.insert(data.vertices.end(), {
            {a,n,tangent,{0,1}}, {b,n,tangent,{0,0}}, {c,n,tangent,{1,0}}, {d,n,tangent,{1,1}}
        });
        data.indices.insert(data.indices.end(), { start, start + 1, start + 2, start, start + 2, start + 3 });
    };
    face({-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f},{0,0,-1},tNX);
    face({.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f},{-.5f,-.5f,.5f},{0,0,1},tX);
    face({-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,-.5f},{-1,0,0},tZ);
    face({.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f},{1,0,0},tNZ);
    face({-.5f,.5f,-.5f},{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f},{0,1,0},tX);
    face({-.5f,-.5f,.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f},{0,-1,0},tX);
    data.materials.emplace_back();
    data.submeshes.push_back({ 0, static_cast<uint32_t>(data.indices.size()), 0 });
    return data;
}

MeshData Mesh::BillboardData()
{
    MeshData data;
    // Прямоугольник смотрит вдоль локальной оси -Z; его матрица разворачивается к камере.
    data.vertices = {
        {{-.5f,-.5f,0},{0,0,-1},{1,0,0,1},{0,1}},
        {{-.5f, .5f,0},{0,0,-1},{1,0,0,1},{0,0}},
        {{ .5f, .5f,0},{0,0,-1},{1,0,0,1},{1,0}},
        {{ .5f,-.5f,0},{0,0,-1},{1,0,0,1},{1,1}}
    };
    data.indices = { 0, 1, 2, 0, 2, 3 };
    data.materials.emplace_back();
    data.submeshes.push_back({ 0, static_cast<uint32_t>(data.indices.size()), 0 });
    return data;
}

MeshData Mesh::TessellatedQuadData()
{
    MeshData data;
    data.vertices = {
        {{-1,0,-1},{0,1,0},{1,0,0,1},{0,1}},
        {{-1,0, 1},{0,1,0},{1,0,0,1},{0,0}},
        {{ 1,0, 1},{0,1,0},{1,0,0,1},{1,0}},
        {{ 1,0,-1},{0,1,0},{1,0,0,1},{1,1}}
    };
    data.indices = { 0, 1, 2, 3 };
    data.materials.emplace_back();
    data.submeshes.push_back({ 0, 4, 0 });
    return data;
}

MeshData Mesh::LoadObj(const std::filesystem::path& path)
{
    std::ifstream file(path);
    if (not file)
        throw std::runtime_error("Cannot open OBJ: " + path.string());

    MeshData data;
    std::vector<XMFLOAT3> positions, normals;
    std::vector<XMFLOAT2> texcoords;
    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> vertexCache;
    std::unordered_map<std::string, uint32_t> materialIndices;
    uint32_t currentMaterial = 0;
    data.materials.emplace_back();
    data.submeshes.push_back({ 0, 0, 0 });

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() or line[0] == '#') continue;
        std::stringstream stream(line);
        std::string command;
        stream >> command;
        if (command == "v")
        {
            XMFLOAT3 value{}; stream >> value.x >> value.y >> value.z; positions.push_back(value);
        }
        else if (command == "vn")
        {
            XMFLOAT3 value{}; stream >> value.x >> value.y >> value.z; normals.push_back(value);
        }
        else if (command == "vt")
        {
            XMFLOAT2 value{}; stream >> value.x >> value.y; value.y = 1.0f - value.y; texcoords.push_back(value);
        }
        else if (command == "mtllib")
        {
            std::string name; stream >> name;
            auto loaded = LoadMaterials(path.parent_path() / name);
            if (not loaded.empty())
            {
                data.materials = std::move(loaded);
                materialIndices.clear();
                for (uint32_t i = 0; i < data.materials.size(); ++i)
                    materialIndices[data.materials[i].name] = i;
            }
        }
        else if (command == "usemtl")
        {
            std::string name; stream >> name;
            const auto found = materialIndices.find(name);
            currentMaterial = found == materialIndices.end() ? 0 : found->second;
            if (data.submeshes.back().indexCount == 0)
                data.submeshes.back().materialIndex = currentMaterial;
            else
                data.submeshes.push_back({ static_cast<uint32_t>(data.indices.size()), 0, currentMaterial });
        }
        else if (command == "f")
        {
            std::vector<uint32_t> polygon;
            std::string token;
            while (stream >> token)
            {
                ObjKey key = ParseFaceIndex(token);
                const auto found = vertexCache.find(key);
                if (found not_eq vertexCache.end())
                {
                    polygon.push_back(found->second);
                    continue;
                }
                Vertex vertex{};
                vertex.position = positions.at(ResolveIndex(key.p, positions.size()));
                vertex.uv = key.t ? texcoords.at(ResolveIndex(key.t, texcoords.size())) : XMFLOAT2{};
                vertex.normal = key.n ? normals.at(ResolveIndex(key.n, normals.size())) : XMFLOAT3{ 0, 1, 0 };
                vertex.tangent = { 1, 0, 0, 1 };
                const uint32_t index = static_cast<uint32_t>(data.vertices.size());
                data.vertices.push_back(vertex);
                vertexCache[key] = index;
                polygon.push_back(index);
            }
            for (size_t i = 1; i + 1 < polygon.size(); ++i)
            {
                data.indices.insert(data.indices.end(), { polygon[0], polygon[i], polygon[i + 1] });
                data.submeshes.back().indexCount += 3;
            }
        }
    }
    data.submeshes.erase(std::remove_if(data.submeshes.begin(), data.submeshes.end(),
        [](const Submesh& mesh) { return mesh.indexCount == 0; }), data.submeshes.end());
    if (data.vertices.empty() or data.indices.empty())
        throw std::runtime_error("OBJ contains no geometry: " + path.string());
    CalculateTangents(data);
    return data;
}

void Mesh::Upload(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const MeshData& data)
{

    const UINT64 vertexSize = data.vertices.size() * sizeof(Vertex);
    const UINT64 indexSize = data.indices.size() * sizeof(uint32_t);
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT); // Основные буферы читает GPU
    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD); // Промежуточные буферы заполняет CPU
    auto vertexDesc = BufferDescription(vertexSize);
    auto indexDesc = BufferDescription(indexSize);

    // COPY_DEST означает, что первой операцией над default-буфером будет копирование.
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_vertexBuffer)), "Create vertex buffer");
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_vertexUpload)), "Create vertex upload");
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_indexBuffer)), "Create index buffer");
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &indexDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_indexUpload)), "Create index upload");

    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 }; 
    ThrowIfFailed(m_vertexUpload->Map(0, &readRange, &mapped), "Map vertex upload");
    memcpy(mapped, data.vertices.data(), static_cast<size_t>(vertexSize));
    m_vertexUpload->Unmap(0, nullptr);
    ThrowIfFailed(m_indexUpload->Map(0, &readRange, &mapped), "Map index upload");
    memcpy(mapped, data.indices.data(), static_cast<size_t>(indexSize));
    m_indexUpload->Unmap(0, nullptr);

    commandList->CopyBufferRegion(m_vertexBuffer.Get(), 0, m_vertexUpload.Get(), 0, vertexSize);
    commandList->CopyBufferRegion(m_indexBuffer.Get(), 0, m_indexUpload.Get(), 0, indexSize);
    std::array<D3D12_RESOURCE_BARRIER, 2> barriers{
        TransitionBarrier(m_vertexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER),
        TransitionBarrier(m_indexBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER)
    };
    commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    m_vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexView.SizeInBytes = static_cast<UINT>(vertexSize);
    m_vertexView.StrideInBytes = sizeof(Vertex);
    m_indexView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    m_indexView.SizeInBytes = static_cast<UINT>(indexSize);
    m_indexView.Format = DXGI_FORMAT_R32_UINT;
    m_indexCount = static_cast<uint32_t>(data.indices.size());
    BoundingBox::CreateFromPoints(m_bounds, data.vertices.size(),
                                  &data.vertices.front().position, sizeof(Vertex));
    m_submeshes = data.submeshes;
    m_materials = data.materials;
}

void Mesh::Bind(ID3D12GraphicsCommandList* commandList) const
{
    commandList->IASetVertexBuffers(0, 1, &m_vertexView);
    commandList->IASetIndexBuffer(&m_indexView);
}
