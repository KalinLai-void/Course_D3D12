#pragma once
#include <windows.h>
#include <DirectXMath.h>

class InputManager {
public:
    static InputManager& Get() {
        static InputManager instance;
        return instance;
    }

    void UpdateMouse(float dx, float dy) {
        m_mouseDeltaX += dx;
        m_mouseDeltaY += dy;
    }

    void SetKeyState(UINT8 key, bool isDown) {
        m_keys[key] = isDown;
    }

    bool IsKeyDown(UINT8 key) const { return m_keys[key]; }
    float GetMouseDeltaX() const { return m_mouseDeltaX; }
    float GetMouseDeltaY() const { return m_mouseDeltaY; }

    const bool* GetKeys() const { return m_keys; }

    bool IsKeyJustPressed(UINT8 key) {
        bool isDown = m_keys[key];
        bool wasDown = m_prevKeys[key];
        m_prevKeys[key] = isDown;
        return isDown && !wasDown;
    }

    void EndFrame() {
        m_mouseDeltaX = 0;
        m_mouseDeltaY = 0;
        memcpy(m_prevKeys, m_keys, sizeof(m_keys));
    }

private:
    InputManager() : m_mouseDeltaX(0), m_mouseDeltaY(0) {
        memset(m_keys, 0, sizeof(m_keys));
        memset(m_prevKeys, 0, sizeof(m_prevKeys));
    }

    bool m_keys[256];
    bool m_prevKeys[256];
    float m_mouseDeltaX;
    float m_mouseDeltaY;
};