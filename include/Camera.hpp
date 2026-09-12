#pragma once
#include "Common.hpp"
#include "InputDevice.hpp"

class Camera
{
public:
    Camera(float aspectRatio);
    void Update(const InputDevice& input, DirectX::XMFLOAT2 mouseDelta, float deltaTime);

    DirectX::XMMATRIX View() const;
    DirectX::XMMATRIX Projection() const;
    DirectX::XMMATRIX ViewProjection() const;
    DirectX::XMFLOAT3 Position() const { return m_position; }
    DirectX::XMFLOAT3 Direction() const { return Forward(); }
    float AspectRatio() const { return m_aspect; }
    float FieldOfView() const { return DirectX::XMConvertToRadians(65.0f); }
    float NearPlane() const { return 0.1f; }
    float FarPlane() const { return 250.0f; }
    DirectX::BoundingFrustum WorldFrustum() const;

private:
    DirectX::XMFLOAT3 Forward() const;
    DirectX::XMFLOAT3 m_position{ 0.0f, 2.5f, -10.0f };
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
    float m_aspect = 16.0f / 9.0f;
};
