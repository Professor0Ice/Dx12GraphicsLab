#include "Texture.hpp"

namespace
{
    std::string NextToken(std::istream& stream)
    {
        std::string token;
        while (stream >> token)
        {
            if (!token.empty() && token[0] == '#')
            {
                std::string rest;
                std::getline(stream, rest);
                continue;
            }
            return token;
        }
        return {};
    }
}

void Texture::LoadPpm(ID3D12Device* device, ID3D12GraphicsCommandList* commandList,
                      const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open texture: " + path.string());
    const std::string magic = NextToken(file);
    if (magic != "P3" && magic != "P6")
        throw std::runtime_error("Only P3/P6 PPM textures are supported: " + path.string());
    const UINT width = static_cast<UINT>(std::stoul(NextToken(file)));
    const UINT height = static_cast<UINT>(std::stoul(NextToken(file)));
    const int maxValue = std::stoi(NextToken(file));
    if (!width || !height || maxValue <= 0)
        throw std::runtime_error("Invalid PPM header: " + path.string());

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    if (magic == "P6")
    {
        file.get();
        std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
        file.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        for (size_t i = 0, j = 0; i < rgb.size(); i += 3, j += 4)
        {
            rgba[j] = rgb[i]; rgba[j + 1] = rgb[i + 1]; rgba[j + 2] = rgb[i + 2]; rgba[j + 3] = 255;
        }
    }
    else
    {
        for (size_t i = 0; i < rgba.size(); i += 4)
        {
            rgba[i] = static_cast<uint8_t>(std::stoi(NextToken(file)) * 255 / maxValue);
            rgba[i + 1] = static_cast<uint8_t>(std::stoi(NextToken(file)) * 255 / maxValue);
            rgba[i + 2] = static_cast<uint8_t>(std::stoi(NextToken(file)) * 255 / maxValue);
            rgba[i + 3] = 255;
        }
    }

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource)), "Create texture");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0, uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowSize, &uploadSize);
    auto uploadDesc = BufferDescription(uploadSize);
    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload)), "Create texture upload");

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(m_upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map texture upload");
    const size_t sourcePitch = static_cast<size_t>(width) * 4;
    for (UINT y = 0; y < height; ++y)
        memcpy(mapped + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
               rgba.data() + static_cast<size_t>(y) * sourcePitch, sourcePitch);
    m_upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = m_resource.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = m_upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    auto barrier = TransitionBarrier(m_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    commandList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = textureDesc.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_resource.Get(), &srv, srvHandle);
}

