#include "pch.h"
#include "Camera.h"
#include "Input/InputManager.h"
#include <Windows.h>
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

void Camera::InitializeFixed(const XMMATRIX& view, const XMMATRIX& proj)
{
	mFixedView = view;
	mProj = proj;
    mUseFixed = true;
}

void Camera::InitializeFree(XMFLOAT3 pos, float yaw, float pitch, float fovY, float aspect, float nearZ, float farZ)
{
	mPosition = pos;
	mYaw = yaw;
	mPitch = pitch;
	mFovY = fovY;
	mAspect = aspect;
	mNearZ = nearZ;
	mFarZ = farZ;
	mProj = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
    mUseFixed = false;
}

void Camera::Update(float dt, const InputManager& input)
{
    if (mUseFixed)
        return;

    // movement
    float moveSpeed = 5.0f;
    if (input.IsKeyDown(VK_SHIFT)) moveSpeed *= 2.0f;

    // read keyboard from input manager
    bool w = input.IsKeyDown('W');
    bool s = input.IsKeyDown('S');
    bool a = input.IsKeyDown('A');
    bool d = input.IsKeyDown('D');
    bool space = input.IsKeyDown(VK_SPACE);
    bool shift = input.IsKeyDown(VK_SHIFT);

    // arrows for rotation
    bool left = input.IsKeyDown(VK_LEFT);
    bool right = input.IsKeyDown(VK_RIGHT);
    bool up = input.IsKeyDown(VK_UP);
    bool down = input.IsKeyDown(VK_DOWN);

    // zoom via mouse wheel or Z/X keys
    int wheel = input.GetMouseWheelDelta();
    bool zoomIn = input.IsKeyDown('Z') || wheel > 0;
    bool zoomOut = input.IsKeyDown('X') || wheel < 0;

    const float yawSpeed = XM_PI;   // rad/s
    const float pitchSpeed = XM_PI; // rad/s
    const float zoomSpeed = 1.0f;   // rad/s for fov change

    if (left) mYaw += yawSpeed * dt;
    if (right) mYaw -= yawSpeed * dt;
    if (up) mPitch += pitchSpeed * dt;
    if (down) mPitch -= pitchSpeed * dt;

    const float limit = XM_PIDIV2 - 0.01f;
    mPitch = std::clamp(mPitch, -limit, limit);

    if (zoomIn) mFovY -= zoomSpeed * dt;
    if (zoomOut) mFovY += zoomSpeed * dt;
    mFovY = std::clamp(mFovY, 0.2f, 2.5f);

    // compute front vector and movement
    float cy = cosf(mYaw), sy = sinf(mYaw), cp = cosf(mPitch), sp = sinf(mPitch);
    XMVECTOR front = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    front = XMVector3Normalize(front);
    XMVECTOR worldUp = XMLoadFloat3(&mWorldUp);
    XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(worldUp, front));
    XMVECTOR upVec = XMVector3Normalize(XMVector3Cross(rightVec, front));

    XMVECTOR movement = XMVectorZero();
    if (w) movement = XMVectorAdd(movement, front);
    if (s) movement = XMVectorSubtract(movement, front);
    if (d) movement = XMVectorAdd(movement, rightVec);
    if (a) movement = XMVectorSubtract(movement, rightVec);
    if (space) movement = XMVectorAdd(movement, worldUp);
    if (shift) movement = XMVectorSubtract(movement, worldUp);

    if (!XMVector3Equal(movement, XMVectorZero()))
    {
        movement = XMVector3Normalize(movement);
        movement = XMVectorScale(movement, moveSpeed * dt);
        XMVECTOR pos = XMLoadFloat3(&mPosition);
        pos = XMVectorAdd(pos, movement);
        XMStoreFloat3(&mPosition, pos);
    }

    // update projection
    mProj = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
}

XMMATRIX Camera::GetViewProjection() const
{
    if (mUseFixed)
    {
        return mFixedView * mProj;
    }

    // compute view from position + yaw/pitch
    float cy = cosf(mYaw);
    float sy = sinf(mYaw);
    float cp = cosf(mPitch);
    float sp = sinf(mPitch);
    XMVECTOR front = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    front = XMVector3Normalize(front);
    XMVECTOR pos = XMLoadFloat3(&mPosition);
    XMVECTOR up = XMLoadFloat3(&mWorldUp);
    XMVECTOR lookTo = XMMatrixLookToLH(pos, front, up).r[0]; 

    XMMATRIX view = XMMatrixLookToLH(pos, front, up);
    return view * mProj;
}