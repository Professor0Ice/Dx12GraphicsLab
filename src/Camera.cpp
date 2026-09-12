#include "Camera.hpp"

using namespace DirectX;

Camera::Camera(float aspectRatio) : m_aspect(aspectRatio) {}

XMFLOAT3 Camera::Forward() const
{
    XMFLOAT3 direction{};
    XMStoreFloat3(&direction, XMVector3Normalize(XMVectorSet(
        std::cos(m_pitch) * std::sin(m_yaw),
        std::sin(m_pitch),
        std::cos(m_pitch) * std::cos(m_yaw), 0.0f)));
    return direction;
}

void Camera::Update(const InputDevice& input, XMFLOAT2 mouseDelta, float deltaTime)
{
    constexpr float sensitivity = 0.0025f;
    m_yaw += mouseDelta.x * sensitivity;
    m_pitch = std::clamp(m_pitch - mouseDelta.y * sensitivity, -XM_PIDIV2 + 0.02f, XM_PIDIV2 - 0.02f);

    const XMFLOAT3 forwardFloat = Forward();
    XMVECTOR forward = XMLoadFloat3(&forwardFloat);
    XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
    XMVECTOR movement = XMVectorZero();
    if (input.IsDown('W')) movement += forward;
    if (input.IsDown('S')) movement -= forward;
    if (input.IsDown('D')) movement += right;
    if (input.IsDown('A')) movement -= right;
    if (input.IsDown(VK_SPACE)) movement += XMVectorSet(0, 1, 0, 0);
    if (input.IsDown(VK_CONTROL)) movement -= XMVectorSet(0, 1, 0, 0);

    if (!XMVector3Equal(movement, XMVectorZero()))
    {
        movement = XMVector3Normalize(movement);
        const float speed = input.IsDown(VK_SHIFT) ? 15.0f : 6.0f;
        XMVECTOR position = XMLoadFloat3(&m_position) + movement * speed * deltaTime;
        XMStoreFloat3(&m_position, position);
    }
}

XMMATRIX Camera::View() const
{
    const XMFLOAT3 forward = Forward();
    return XMMatrixLookToLH(XMLoadFloat3(&m_position), XMLoadFloat3(&forward), XMVectorSet(0, 1, 0, 0));
}

XMMATRIX Camera::Projection() const
{
    return XMMatrixPerspectiveFovLH(FieldOfView(), m_aspect, NearPlane(), FarPlane());
}

XMMATRIX Camera::ViewProjection() const
{
    return View() * Projection();
}

BoundingFrustum Camera::WorldFrustum() const
{
    BoundingFrustum viewFrustum;
    BoundingFrustum::CreateFromMatrix(viewFrustum, Projection());
    BoundingFrustum worldFrustum;
    XMMATRIX inverseView = XMMatrixInverse(nullptr, View());
    viewFrustum.Transform(worldFrustum, inverseView);
    return worldFrustum;
}
