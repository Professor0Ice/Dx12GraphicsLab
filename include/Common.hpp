#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

//штука превращающая ошибки в исключения
inline void ThrowIfFailed(HRESULT hr, const char* operation)
{
    if (FAILED(hr))
    {
        char text[256]{};
        sprintf_s(text, "%s failed (HRESULT 0x%08X)", operation, static_cast<unsigned>(hr));
        throw std::runtime_error(text);
    }
}

inline UINT64 AlignConstantBuffer(UINT64 size)
{
    // Возведение буфера до нужного размера (256 байт треб. директа)
    return (size + D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1) &
           ~(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - 1);
}


inline D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE type)
{
    D3D12_HEAP_PROPERTIES result{};
    result.Type = type; // DEFAULT хранится на GPU, UPLOAD доступен процессору для загрузки данных.
    result.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN; // Конкретный тип страниц выбирает драйвер.
    result.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN; // Пул памяти также выбирает драйвер.
    result.CreationNodeMask = 1; // Ресурс создаётся на первом GPU системы.
    result.VisibleNodeMask = 1; // Ресурс виден первому GPU.
    return result;
}

// Создаёт описание обычного буфера указанного размера.
inline D3D12_RESOURCE_DESC BufferDescription(UINT64 size)
{
    D3D12_RESOURCE_DESC result{};
    result.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; // Ресурс является буфером, а не текстурой.
    result.Width = size; // размер в байтах.
    result.Height = 1; // У линейного буфера нет высоты.
    result.DepthOrArraySize = 1; // Буфер не является массивом текстур.
    result.MipLevels = 1; // MIP-уровни применимы к текстурам, поэтому оставляем один.
    result.Format = DXGI_FORMAT_UNKNOWN; // Формат элементов задаётся позже через view.
    result.SampleDesc.Count = 1; // MSAA для обычного буфера не используется.
    result.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; // Байты лежат последовательно.
    return result;
}

// когда ресурс перестаёт быть целью копирования/рисования и начинает читаться шейдером.
inline D3D12_RESOURCE_BARRIER TransitionBarrier(
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; // Это барьер смены состояния.
    barrier.Transition.pResource = resource; // Указывает конкретный GPU-ресурс.
    barrier.Transition.StateBefore = before; // Состояние, в котором ресурс находится сейчас.
    barrier.Transition.StateAfter = after; // Состояние, нужное следующей команде GPU.
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; // Меняются все части ресурса.
    return barrier;
}

inline std::filesystem::path ExecutableDirectory()
{
    std::array<wchar_t, MAX_PATH> path{};
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), size)).parent_path();
}
