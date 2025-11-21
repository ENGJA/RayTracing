#include "pch.h"
#include "CameraManager.h"
#include "Input/InputManager.h"
#include <DirectXMath.h>


using DirectX::XMMATRIX;
using DirectX::XMMatrixLookAtLH;
using DirectX::XMMatrixTranslation;
using DirectX::XMMatrixPerspectiveFovLH;

void CameraManager::Initialize(UINT width, UINT height)
{
    mCameras.clear();

    // fixed view (te same parametry co by³y w Renderer)
    XMMATRIX viewMatrix = XMMatrixLookAtLH(
        { 0.0f, 1.0f, -3.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f });
    XMMATRIX translation = XMMatrixTranslation(0.0f, -1.0f, 1.0f);
    viewMatrix = translation * viewMatrix;
    XMMATRIX projectionMatrix = XMMatrixPerspectiveFovLH(1.2217304764f, static_cast<float>(width) / static_cast<float>(height), 1.0f, 50.0f);

    mCameras.emplace_back();
    mCameras[0].InitializeFixed(viewMatrix, projectionMatrix);

    // free / controllable camera (te same parametry co by³y w Renderer)
    mCameras.emplace_back();
    mCameras[1].InitializeFree({ 3.0f, 1.0f, -3.0f }, /*yaw*/2.5f, /*pitch*/0.0f,
        /*fovY*/1.3217304764f,
        static_cast<float>(width) / static_cast<float>(height),
        1.0f, 50.0f);

    mActiveIndex = 0;

    // register input callbacks with InputManager
    auto& input = InputManager::Instance;

    // toggle camera on 'C' edge press
    input.RegisterKeyPressedCallback('C', [this]() {
        if (!mCameras.empty())
            mActiveIndex = (mActiveIndex + 1) % mCameras.size();
        });

    // movement & rotation callbacks (continuous while key down)
    const float baseMoveSpeed = 5.0f; // units per second
    const float yawSpeed = 3.1415f;   // rad/s
    const float pitchSpeed = 3.1415f; // rad/s
    const float zoomSpeed = 1.0f;   // rad/s for fov change

    // forward / back
    input.RegisterKeyDownCallback('W', [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(speed * dt, 0.0f, 0.0f);
        });
    input.RegisterKeyDownCallback('S', [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(-speed * dt, 0.0f, 0.0f);
        });

    // right / left
    input.RegisterKeyDownCallback('D', [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(0.0f, speed * dt, 0.0f);
        });
    input.RegisterKeyDownCallback('A', [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(0.0f, -speed * dt, 0.0f);
        });

    // up / down (space / shift)
    input.RegisterKeyDownCallback(VK_SPACE, [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(0.0f, 0.0f, speed * dt);
        });
	input.RegisterKeyDownCallback(VK_SHIFT, [this, baseMoveSpeed](float dt) {
        float speed = baseMoveSpeed;
        if (!mCameras.empty())
            mCameras[mActiveIndex].MoveLocal(0.0f, 0.0f, -speed * dt);
		});

    // arrow rotation
    input.RegisterKeyDownCallback(VK_LEFT, [this, yawSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].AddYaw(yawSpeed * dt);
        });
    input.RegisterKeyDownCallback(VK_RIGHT, [this, yawSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].AddYaw(-yawSpeed * dt);
        });
    input.RegisterKeyDownCallback(VK_UP, [this, pitchSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].AddPitch(pitchSpeed * dt);
        });
    input.RegisterKeyDownCallback(VK_DOWN, [this, pitchSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].AddPitch(-pitchSpeed * dt);
        });

    // zoom via Z/X keys
    input.RegisterKeyDownCallback('Z', [this, zoomSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].ChangeFov(-zoomSpeed * dt);
        });
    input.RegisterKeyDownCallback('X', [this, zoomSpeed](float dt) {
        if (!mCameras.empty())
            mCameras[mActiveIndex].ChangeFov(zoomSpeed * dt);
        });

    // mouse move -> rotation
    const float mouseSensitivity = 0.0025f;
    input.RegisterMouseMoveCallback([this, mouseSensitivity](DirectX::XMFLOAT2 delta) {
        if (delta.x == 0.0f && delta.y == 0.0f) return;
        if (mCameras.empty()) return;
        mCameras[mActiveIndex].AddYaw(-delta.x * mouseSensitivity);
        mCameras[mActiveIndex].AddPitch(-delta.y * mouseSensitivity);
        });

    // wheel -> fov
    const float wheelZoomSpeed = 0.0015f;
    input.RegisterMouseWheelCallback([this, wheelZoomSpeed](int wheel) {
        if (wheel == 0) return;
        if (mCameras.empty()) return;
        mCameras[mActiveIndex].ChangeFov(-static_cast<float>(wheel) * wheelZoomSpeed);
        });
}

void CameraManager::Update(float dt)
{
    // Przetwórz wszystkie zarejestrowane callbacki (klawisze, przytrzymania, ruch myszy, kó³ko)
    InputManager::Instance.ProcessCallbacks(dt);
}

DirectX::XMMATRIX CameraManager::GetActiveViewProjection() const
{
    if (mCameras.empty())
        return DirectX::XMMatrixIdentity();
    return mCameras[mActiveIndex].GetViewProjection();
}