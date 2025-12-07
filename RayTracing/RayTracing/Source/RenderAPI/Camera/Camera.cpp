#include "pch.h"
#include "Camera.h"
#include <cmath>

using namespace DirectX;

void Camera::Initialize(CameraType type, const CameraParams& params)
{
    mType = type;
    mFovY = params.fovY;
    mAspect = params.aspect;
    mNearZ = params.nearZ;
    mFarZ = params.farZ;
    
    // Build projection matrix
    mProj = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);

    if (type == CameraType::Fixed)
    {
        // Build view matrix from lookFrom, lookAt, up
        XMVECTOR eye = XMLoadFloat3(&params.lookFrom);
        XMVECTOR target = XMLoadFloat3(&params.lookAt);
        XMVECTOR up = XMLoadFloat3(&params.up);
        mFixedView = XMMatrixLookAtLH(eye, target, up);
    }
    else // CameraType::Free
    {
        mPosition = params.position;
        mYaw = params.yaw;
        mPitch = params.pitch;
    }
}

void Camera::AddYaw(float delta)
{
    if (mType == CameraType::Fixed) return;
    mYaw += delta;
}

void Camera::AddPitch(float delta)
{
    if (mType == CameraType::Fixed) return;
    const float limit = XM_PIDIV2 - 0.01f;
    mPitch = std::clamp(mPitch + delta, -limit, limit);
}

void Camera::MoveLocal(float forward, float right, float up)
{
    if (mType == CameraType::Fixed) return;

    // Compute orientation basis from yaw/pitch
    float cy = cosf(mYaw);
    float sy = sinf(mYaw);
    float cp = cosf(mPitch);
    float sp = sinf(mPitch);
    
    XMVECTOR frontDir = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    frontDir = XMVector3Normalize(frontDir);
    
    XMVECTOR worldUp = XMLoadFloat3(&mWorldUp);
    XMVECTOR rightDir = XMVector3Normalize(XMVector3Cross(worldUp, frontDir));
    XMVECTOR upDir = XMVector3Cross(frontDir, rightDir);

    // Calculate movement vector
    XMVECTOR movement = XMVectorZero();
    movement = XMVectorAdd(movement, XMVectorScale(frontDir, forward));
    movement = XMVectorAdd(movement, XMVectorScale(rightDir, right));
    movement = XMVectorAdd(movement, XMVectorScale(worldUp, up));

    // Apply movement
    XMVECTOR pos = XMLoadFloat3(&mPosition);
    pos = XMVectorAdd(pos, movement);
    XMStoreFloat3(&mPosition, pos);
}

void Camera::ChangeFov(float delta)
{
    if (mType == CameraType::Fixed) return;
    
    mFovY = std::clamp(mFovY + delta, 0.2f, 2.5f);
    mProj = XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
}

XMMATRIX Camera::GetViewProjection() const
{
    if (mType == CameraType::Fixed)
    {
        return mFixedView * mProj;
    }

    // Free camera - compute view from position and orientation
    float cy = cosf(mYaw);
    float sy = sinf(mYaw);
    float cp = cosf(mPitch);
    float sp = sinf(mPitch);
    
    XMVECTOR frontDir = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    frontDir = XMVector3Normalize(frontDir);
    
    XMVECTOR pos = XMLoadFloat3(&mPosition);
    XMVECTOR up = XMLoadFloat3(&mWorldUp);
    
    XMMATRIX view = XMMatrixLookToLH(pos, frontDir, up);
    return view * mProj;
}

XMFLOAT3 Camera::GetPosition() const
{
    if (mType == CameraType::Fixed)
    {
        // Extract position from inverse of view matrix
        XMMATRIX invView = XMMatrixInverse(nullptr, mFixedView);
        XMFLOAT3 pos;
        XMStoreFloat3(&pos, invView.r[3]);
        return pos;
    }
    
    return mPosition;
}

XMFLOAT3 Camera::GetForward() const
{
    if (mType == CameraType::Fixed)
    {
        // Extract forward direction from view matrix
        // Forward is -Z axis of the inverse view matrix
        XMMATRIX invView = XMMatrixInverse(nullptr, mFixedView);
        XMVECTOR forwardVec = -invView.r[2];
        forwardVec = XMVector3Normalize(forwardVec);
        
        XMFLOAT3 forward;
        XMStoreFloat3(&forward, forwardVec);
        return forward;
    }

    // Free camera - compute from yaw/pitch
    float cy = cosf(mYaw);
    float sy = sinf(mYaw);
    float cp = cosf(mPitch);
    float sp = sinf(mPitch);
    
    XMVECTOR frontDir = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
    frontDir = XMVector3Normalize(frontDir);
    
    XMFLOAT3 forward;
    XMStoreFloat3(&forward, frontDir);
    return forward;
}

void Camera::OnResize(UINT width, UINT height)
{
    if (width == 0 || height == 0)
        return;

    mAspect = static_cast<float>(width) / static_cast<float>(height);
    mProj = DirectX::XMMatrixPerspectiveFovLH(mFovY, mAspect, mNearZ, mFarZ);
}