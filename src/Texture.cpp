#include "Texture.hpp"
#include <wincodec.h>

namespace
{
    constexpr uint32_t DdsMagic = 0x20534444;
    constexpr uint32_t DdpfRgb = 0x00000040;
    constexpr uint32_t DdsCaps2Cubemap = 0x00000200;
    constexpr uint32_t DdsCaps2AllFaces = 0x0000FC00;

#pragma pack(push, 1)
    struct DdsPixelFormat
    {
        uint32_t size;
        uint32_t flags;
        uint32_t fourCc;
        uint32_t rgbBitCount;
        uint32_t rMask;
        uint32_t gMask;
        uint32_t bMask;
        uint32_t aMask;
    };

    struct DdsHeader
    {
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        uint32_t pitchOrLinearSize;
        uint32_t depth;
        uint32_t mipMapCount;
        uint32_t reserved1[11];
        DdsPixelFormat pixelFormat;
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
        uint32_t reserved2;
    };
#pragma pack(pop)

    static_assert(sizeof(DdsPixelFormat) == 32);
    static_assert(sizeof(DdsHeader) == 124);

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

void Texture::LoadPng(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle, bool heightMap)
{
    const HRESULT initializeResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initializeResult) && initializeResult != RPC_E_CHANGED_MODE)
        ThrowIfFailed(initializeResult, "Initialize COM for PNG loading");

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "Create WIC factory");
    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder), "Open PNG texture");
    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, &frame), "Decode PNG frame");

    UINT width = 0, height = 0;
    ThrowIfFailed(frame->GetSize(&width, &height), "Read PNG dimensions");
    if (width == 0 || height == 0)
        throw std::runtime_error("Invalid PNG dimensions: " + path.string());

    const WICPixelFormatGUID targetFormat = heightMap ? GUID_WICPixelFormat16bppGray : GUID_WICPixelFormat32bppRGBA;
    const DXGI_FORMAT textureFormat = heightMap ? DXGI_FORMAT_R16_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const UINT bytesPerPixel = heightMap ? 2u : 4u;
    const UINT sourcePitch = width * bytesPerPixel;
    std::vector<uint8_t> pixels(static_cast<size_t>(sourcePitch) * height);

    WICPixelFormatGUID sourceFormat{};
    ThrowIfFailed(frame->GetPixelFormat(&sourceFormat), "Read PNG pixel format");
    if (IsEqualGUID(sourceFormat, targetFormat))
    {
        ThrowIfFailed(frame->CopyPixels(nullptr, sourcePitch, static_cast<UINT>(pixels.size()), pixels.data()), "Copy PNG pixels");
    }
    else
    {
        ComPtr<IWICFormatConverter> converter;
        ThrowIfFailed(factory->CreateFormatConverter(&converter), "Create PNG format converter");
        ThrowIfFailed(converter->Initialize(frame.Get(), targetFormat, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "Convert PNG pixels");
        ThrowIfFailed(converter->CopyPixels(nullptr, sourcePitch, static_cast<UINT>(pixels.size()), pixels.data()), "Copy converted PNG pixels");
    }

    Upload2D(device, commandList, width, height, textureFormat, pixels.data(), sourcePitch, srvHandle);
}

void Texture::LoadDds(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Cannot open DDS texture: " + path.string());

    uint32_t magic = 0;
    DdsHeader header{};
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || magic != DdsMagic || header.size != sizeof(DdsHeader) || header.pixelFormat.size != sizeof(DdsPixelFormat))
        throw std::runtime_error("Invalid DDS header: " + path.string());

    const DdsPixelFormat& format = header.pixelFormat;
    if ((format.flags & DdpfRgb) == 0 || format.rgbBitCount != 32 || format.rMask != 0x000000ff || format.gMask != 0x0000ff00 || format.bMask != 0x00ff0000 || format.aMask != 0xff000000)
        throw std::runtime_error("DDS must use uncompressed RGBA8 data: " + path.string());

    const bool isCube = (header.caps2 & DdsCaps2Cubemap) != 0;
    if (isCube && (header.caps2 & DdsCaps2AllFaces) != DdsCaps2AllFaces)
        throw std::runtime_error("DDS cubemap does not contain all six faces: " + path.string());
    if (header.width == 0 || header.height == 0 || (isCube && header.width != header.height))
        throw std::runtime_error("Invalid DDS dimensions: " + path.string());

    const UINT mipCount = std::max(1u, header.mipMapCount);
    const UINT arraySize = isCube ? 6u : 1u;
    const UINT subresourceCount = mipCount * arraySize;
    std::vector<std::vector<uint8_t>> subresources(subresourceCount);
    for (UINT arraySlice = 0; arraySlice < arraySize; ++arraySlice)
    {
        UINT width = header.width;
        UINT height = header.height;
        for (UINT mip = 0; mip < mipCount; ++mip)
        {
            auto& pixels = subresources[arraySlice * mipCount + mip];
            pixels.resize(static_cast<size_t>(width) * height * 4);
            file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
            if (!file)
                throw std::runtime_error("Truncated DDS texture: " + path.string());
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
    }

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = header.width;
    textureDesc.Height = header.height;
    textureDesc.DepthOrArraySize = static_cast<UINT16>(arraySize);
    textureDesc.MipLevels = static_cast<UINT16>(mipCount);
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource)), "Create DDS texture");

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
    std::vector<UINT> rowCounts(subresourceCount);
    std::vector<UINT64> rowSizes(subresourceCount);
    UINT64 uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, subresourceCount, 0, footprints.data(), rowCounts.data(), rowSizes.data(), &uploadSize);
    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadDesc = BufferDescription(uploadSize);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload)), "Create DDS upload");

    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(m_upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map DDS upload");
    for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
    {
        const auto& footprint = footprints[subresource];
        const auto& pixels = subresources[subresource];
        const size_t sourcePitch = static_cast<size_t>(rowSizes[subresource]);
        for (UINT row = 0; row < rowCounts[subresource]; ++row)
            memcpy(mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch, pixels.data() + static_cast<size_t>(row) * sourcePitch, sourcePitch);
    }
    m_upload->Unmap(0, nullptr);

    for (UINT subresource = 0; subresource < subresourceCount; ++subresource)
    {
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = m_resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = subresource;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = m_upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprints[subresource];
        commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    }

    auto barrier = TransitionBarrier(m_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    commandList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = textureDesc.Format;
    if (isCube)
    {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MostDetailedMip = 0;
        srv.TextureCube.MipLevels = mipCount;
    }
    else
    {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MostDetailedMip = 0;
        srv.Texture2D.MipLevels = mipCount;
    }
    device->CreateShaderResourceView(m_resource.Get(), &srv, srvHandle);
}

void Texture::LoadPpm(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, const std::filesystem::path& path, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
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
    if (!width || !height || maxValue <= 0 || maxValue > 65535)
        throw std::runtime_error("Invalid PPM header: " + path.string());

    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    if (magic == "P6")
    {
        const int delimiter = file.get();
        if (delimiter == '\r' && file.peek() == '\n') file.get();
        const size_t channelCount = static_cast<size_t>(width) * height * 3;
        const size_t bytesPerChannel = maxValue > 255 ? 2 : 1;
        std::vector<uint8_t> rgb(channelCount * bytesPerChannel);
        file.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        if (!file) throw std::runtime_error("Truncated PPM texture: " + path.string());

        for (size_t i = 0; i < rgba.size() / 4; ++i)
        {
            for (size_t channel = 0; channel < 3; ++channel)
            {
                const size_t offset = (i * 3 + channel) * bytesPerChannel;
                unsigned value = rgb[offset];
                if (bytesPerChannel == 2)
                    value = (value << 8) | rgb[offset + 1];
                rgba[i * 4 + channel] = static_cast<uint8_t>(value * 255u / maxValue);
            }
            rgba[i * 4 + 3] = 255;
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

    Upload2D(device, commandList, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, rgba.data(), width * 4, srvHandle);
}

void Texture::Upload2D(ID3D12Device* device, ID3D12GraphicsCommandList* commandList, UINT width, UINT height, DXGI_FORMAT format, const uint8_t* pixels, UINT rowPitch, D3D12_CPU_DESCRIPTOR_HANDLE srvHandle)
{
    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    const auto defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_resource)), "Create texture");

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows = 0;
    UINT64 rowSize = 0, uploadSize = 0;
    device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &footprint, &rows, &rowSize, &uploadSize);
    const auto uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
    const auto uploadDesc = BufferDescription(uploadSize);
    ThrowIfFailed(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_upload)), "Create texture upload");

    uint8_t* mapped = nullptr;
    const D3D12_RANGE readRange{ 0, 0 };
    ThrowIfFailed(m_upload->Map(0, &readRange, reinterpret_cast<void**>(&mapped)), "Map texture upload");
    for (UINT row = 0; row < height; ++row)
        memcpy(mapped + footprint.Offset + static_cast<size_t>(row) * footprint.Footprint.RowPitch, pixels + static_cast<size_t>(row) * rowPitch, rowPitch);
    m_upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = m_resource.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = m_upload.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    source.PlacedFootprint = footprint;
    commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
    const auto barrier = TransitionBarrier(m_resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
    commandList->ResourceBarrier(1, &barrier);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(m_resource.Get(), &srv, srvHandle);
}
