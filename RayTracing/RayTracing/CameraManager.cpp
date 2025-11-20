#include "pch.h"
#include "CameraManager.h"
#include "Input/InputManager.h"

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
}

void CameraManager::Update(float dt)
{
    // toggle active camera z klawisza 'C'
    if (InputManager::Get().WasKeyPressed('C'))
    {
        if (!mCameras.empty())
            mActiveIndex = (mActiveIndex + 1) % mCameras.size();
    }

    if (!mCameras.empty())
    {
        // Update aktywnej kamery (free camera reaguje na input)
        mCameras[mActiveIndex].Update(dt, InputManager::Get());
    }
}

DirectX::XMMATRIX CameraManager::GetActiveViewProjection() const
{
    if (mCameras.empty())
        return DirectX::XMMatrixIdentity();
    return mCameras[mActiveIndex].GetViewProjection();
}