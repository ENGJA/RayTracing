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

void Camera::AddYaw(float delta)
{
    mYaw += delta;
}

void Camera::AddPitch(float delta)
{
    const float limit = XM_PIDIV2 - 0.01f;
    mPitch = std::clamp(mPitch + delta, -limit, limit);
}

void Camera::MoveLocal(float forward, float right, float up)
{
    // compute orientation basis
    float cy = cosf(mYaw), sy = sinf(mYaw), cp = cosf(mPitch), sp = sinf(mPitch);
    XMVECTOR front = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    front = XMVector3Normalize(front);
    XMVECTOR worldUp = XMLoadFloat3(&mWorldUp);
    XMVECTOR rightVec = XMVector3Normalize(XMVector3Cross(worldUp, front));
    XMVECTOR upVec = XMVector3Normalize(XMVector3Cross(rightVec, front));

    XMVECTOR movement = XMVectorZero();
    if (forward != 0.0f) movement = XMVectorAdd(movement, XMVectorScale(front, forward));
    if (right != 0.0f) movement = XMVectorAdd(movement, XMVectorScale(rightVec, right));
    if (up != 0.0f) movement = XMVectorAdd(movement, XMVectorScale(worldUp, up));

    XMVECTOR pos = XMLoadFloat3(&mPosition);
    pos = XMVectorAdd(pos, movement);
    XMStoreFloat3(&mPosition, pos);
}

void Camera::ChangeFov(float delta)
{
    mFovY = std::clamp(mFovY + delta, 0.2f, 2.5f);
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