#include "stdafx.h"
#include "Camera.h"
#include "InputManager.h"
#include "Win32Application.h"

using namespace DirectX;

Camera::Camera() :
    m_aspectRatio(1.77f), m_yaw(90.0f), m_pitch(0.0f),
    m_moveSpeed(100.0f), m_sensitivity(0.1f)
{
    m_position = { 0.0f, 20.0f, 0.0f };
    m_worldUp = { 0.0f, 1.0f, 0.0f };
}

void Camera::Initialize(float aspectRatio) {
    m_aspectRatio = aspectRatio;
    UpdateVectors();
}

void Camera::UpdateVectors() {
    float yawRad = XMConvertToRadians(m_yaw);
    float pitchRad = XMConvertToRadians(m_pitch);

    XMVECTOR front;
    front = XMVectorSet(
        cos(yawRad) * cos(pitchRad),
        sin(pitchRad),
        sin(yawRad) * cos(pitchRad),
        0.0f
    );
    m_front = XMFLOAT3(XMVector3Normalize(front).m128_f32);

    XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&m_worldUp), XMLoadFloat3(&m_front)));
    XMStoreFloat3(&m_right, right);
    XMStoreFloat3(&m_up, XMVector3Cross(XMLoadFloat3(&m_front), right));
}

void Camera::UpdateMouseLock()
{
    HWND hwnd = Win32Application::GetHwnd();
    if (m_isMouseLocked)
    {
        while (::ShowCursor(FALSE) >= 0);

        RECT rect;
        ::GetWindowRect(hwnd, &rect);
        int centerX = rect.left + (rect.right - rect.left) / 2;
        int centerY = rect.top + (rect.bottom - rect.top) / 2;

        ::ClipCursor(&rect);

        POINT currentPos;
        ::GetCursorPos(&currentPos);

        float dx = static_cast<float>(currentPos.x - centerX);
        float dy = static_cast<float>(currentPos.y - centerY);

        InputManager::Get().UpdateMouse(dx, dy);
        ::SetCursorPos(centerX, centerY);
    }
    else
    {
        while (::ShowCursor(TRUE) < 0);
        ::ClipCursor(NULL);
    }
}

void Camera::Update(float deltaTime) {
    auto& input = InputManager::Get();

    float mouseDX = input.GetMouseDeltaX(); 
    float mouseDY = input.GetMouseDeltaY(); 

    m_yaw += -mouseDX * m_sensitivity; 
    m_pitch -= mouseDY * m_sensitivity;

    if (m_pitch > 89.0f) m_pitch = 89.0f;
    if (m_pitch < -89.0f) m_pitch = -89.0f;

    float velocity = m_moveSpeed * deltaTime;
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR front = XMLoadFloat3(&m_front);
    XMVECTOR right = XMLoadFloat3(&m_right);

    if (input.IsKeyDown('W')) pos += front * velocity;
    if (input.IsKeyDown('S')) pos -= front * velocity;
    if (input.IsKeyDown('A')) pos -= right * velocity;
    if (input.IsKeyDown('D')) pos += right * velocity;

    XMStoreFloat3(&m_position, pos);
    UpdateVectors();
}

XMMATRIX Camera::GetViewMatrix() const {
    return XMMatrixLookToLH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_front), XMLoadFloat3(&m_up));
}

XMMATRIX Camera::GetProjectionMatrix() const {
    return XMMatrixPerspectiveFovLH(XM_PIDIV4, m_aspectRatio, 1.0f, 10000.0f);
}

void Camera::Pan(float dx, float dy) {
    float panSpeed = 0.1f;
    XMVECTOR pos = XMLoadFloat3(&m_position);
    XMVECTOR right = XMLoadFloat3(&m_right);
    XMVECTOR up = XMLoadFloat3(&m_up);

    pos -= right * (dx * panSpeed);
    pos += up * (dy * panSpeed);

    XMStoreFloat3(&m_position, pos);
}