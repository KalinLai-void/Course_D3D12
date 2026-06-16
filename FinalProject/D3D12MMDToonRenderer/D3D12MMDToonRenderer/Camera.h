#pragma once
#include <DirectXMath.h>

class Camera {
public:
    static Camera& Get() {
        static Camera instance;
        return instance;
    }

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;

    void Initialize(float aspectRatio);

    bool m_isMouseLocked = true;
    void UpdateMouseLock();

    void Update(float deltaTime);

    DirectX::XMMATRIX GetViewMatrix() const;
    DirectX::XMMATRIX GetProjectionMatrix() const;
    DirectX::XMFLOAT3 GetPosition() const { return m_position; }
    void Pan(float dx, float dy);

private:
    Camera();

    void UpdateVectors();

    DirectX::XMFLOAT3 m_position;
    DirectX::XMFLOAT3 m_front;
    DirectX::XMFLOAT3 m_up;
    DirectX::XMFLOAT3 m_right;
    DirectX::XMFLOAT3 m_worldUp;

    float m_yaw;
    float m_pitch;

    float m_aspectRatio;
    float m_moveSpeed;
    float m_sensitivity;
};