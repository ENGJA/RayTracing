#include "pch.h"
#include "CameraManager.h"
#include "Input/InputManager.h"
#include <DirectXMath.h>

using DirectX::XMMATRIX;

void CameraManager::Initialize(UINT width, UINT height)
{
    mCameras.clear();

    float aspect = static_cast<float>(width) / static_cast<float>(height);

    // Fixed camera
    CameraParams fixedParams;
    fixedParams.lookFrom = { 8.0f, 4.0f, 1.0f };
    fixedParams.lookAt = { 0.0f, 0.0f, 0.0f };
    fixedParams.up = { 0.0f, 1.0f, 0.0f };
    fixedParams.fovY = 1.2217304764f;
    fixedParams.aspect = aspect;
    fixedParams.nearZ = 1.0f;
    fixedParams.farZ = 50.0f;

    mCameras.emplace_back();
    mCameras[0].Initialize(CameraType::Fixed, fixedParams);

    // Free camera
    CameraParams freeParams;
    freeParams.position = { 3.0f, 1.0f, -3.0f };
    freeParams.yaw = 2.5f;
    freeParams.pitch = 0.0f;
    freeParams.fovY = 1.3217304764f;
    freeParams.aspect = aspect;
    freeParams.nearZ = 0.1f;
    freeParams.farZ = 50.0f;

    mCameras.emplace_back();
    mCameras[1].Initialize(CameraType::Free, freeParams);

    mActiveIndex = 0;

    RegisterInputCallbacks();
}

void CameraManager::RegisterInputCallbacks()
{
    auto& input = InputManager::Instance;

    // Toggle camera on 'C' key press
    input.RegisterKeyPressedCallback('C', [this]() {
        if (!mIsActive) return;
        if (!mCameras.empty())
            mActiveIndex = (mActiveIndex + 1) % mCameras.size();
    });

    // Movement & rotation constants
    const float baseMoveSpeed = 5.0f;
    const float yawSpeed = 3.1415f;
    const float pitchSpeed = 3.1415f;
    const float zoomSpeed = 1.0f;

    // Movement callbacks - WASD
    input.RegisterKeyDownCallback('W', [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(speed * dt, 0.0f, 0.0f);
    });

    input.RegisterKeyDownCallback('S', [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(-speed * dt, 0.0f, 0.0f);
    });

    input.RegisterKeyDownCallback('D', [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(0.0f, speed * dt, 0.0f);
    });

    input.RegisterKeyDownCallback('A', [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(0.0f, -speed * dt, 0.0f);
    });

    // Vertical movement - Space/Shift
    input.RegisterKeyDownCallback(VK_SPACE, [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(0.0f, 0.0f, speed * dt);
    });

    input.RegisterKeyDownCallback(VK_SHIFT, [this, baseMoveSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        float speed = baseMoveSpeed * mMoveSpeedMultiplier;
        mCameras[mActiveIndex].MoveLocal(0.0f, 0.0f, -speed * dt);
    });

    // Rotation - Arrow keys
    input.RegisterKeyDownCallback(VK_LEFT, [this, yawSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].AddYaw(yawSpeed * dt);
    });

    input.RegisterKeyDownCallback(VK_RIGHT, [this, yawSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].AddYaw(-yawSpeed * dt);
    });

    input.RegisterKeyDownCallback(VK_UP, [this, pitchSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].AddPitch(pitchSpeed * dt);
    });

    input.RegisterKeyDownCallback(VK_DOWN, [this, pitchSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].AddPitch(-pitchSpeed * dt);
    });

    // FOV adjustment - Z/X keys
    input.RegisterKeyDownCallback('Z', [this, zoomSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].ChangeFov(-zoomSpeed * dt);
    });

    input.RegisterKeyDownCallback('X', [this, zoomSpeed](float dt) {
        if (!mIsActive || mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        mCameras[mActiveIndex].ChangeFov(zoomSpeed * dt);
    });

    // Mouse rotation
    const float mouseSensitivity = 0.0025f;
    input.RegisterMouseMoveCallback([this, mouseSensitivity](DirectX::XMFLOAT2 delta) {
        if (!mIsActive || delta.x == 0.0f && delta.y == 0.0f) return;
        if (mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        
        mCameras[mActiveIndex].AddYaw(-delta.x * mouseSensitivity);
        mCameras[mActiveIndex].AddPitch(-delta.y * mouseSensitivity);
    });

    // Mouse wheel FOV
    const float wheelZoomSpeed = 0.0015f;
    input.RegisterMouseWheelCallback([this, wheelZoomSpeed](int wheel) {
        if (!mIsActive || wheel == 0) return;
        if (mCameras.empty() || mCameras[mActiveIndex].IsFixed()) return;
        
        mCameras[mActiveIndex].ChangeFov(-static_cast<float>(wheel) * wheelZoomSpeed);
    });
}

void CameraManager::Update(float dt)
{
    // Input callbacks are processed in Application::Update
}

DirectX::XMMATRIX CameraManager::GetActiveViewProjection() const
{
    if (mCameras.empty())
        return DirectX::XMMatrixIdentity();
    return mCameras[mActiveIndex].GetViewProjection();
}

DirectX::XMFLOAT3 CameraManager::GetActiveCameraPosition() const
{
    if (mCameras.empty())
        return DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
    return mCameras[mActiveIndex].GetPosition();
}

DirectX::XMFLOAT3 CameraManager::GetActiveCameraForward() const
{
    if (mCameras.empty())
        return DirectX::XMFLOAT3{ 0.0f, 0.0f, 1.0f };
    return mCameras[mActiveIndex].GetForward();
}