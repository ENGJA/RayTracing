#pragma once

#include <vector>
#include <DirectXMath.h>
#include "RenderAPI/Camera/Camera.h"

class CameraManager
{
private:
    std::vector<Camera> mCameras;
    size_t mActiveIndex = 0;

public:
    CameraManager() = default;

    // Inicjalizuje jedn¹ kamerê statyczn¹ i jedn¹ ruchom¹
    void Initialize(UINT width, UINT height);

    // Aktualizuje aktywn¹ kamerê (obs³uga wejœcia, toggling)
    void Update(float dt);

    // Zwraca widok-projekcjê aktywnej kamery
    DirectX::XMMATRIX GetActiveViewProjection() const;
};