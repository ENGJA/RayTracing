#pragma once

#include <vector>
#include <DirectXMath.h>
#include "RenderAPI/Camera/Camera.h"

/**
 * @brief CameraManager holds multiple Camera instances and manages the active one.
 *
 * The manager initializes a fixed and a free camera, registers input callbacks
 * (via InputManager) to control movement/rotation/zoom and to toggle the active camera.
 */
class CameraManager
{
private:
    std::vector<Camera> mCameras;
    size_t mActiveIndex = 0;
    bool mIsActive = true; ///< Controls whether camera processes input

public:
    CameraManager() = default;

    /**
     * @brief Initialize one fixed and one free camera.
     * @param width Render target width used to compute projection aspect ratio.
     * @param height Render target height used to compute projection aspect ratio.
     */
    void Initialize(UINT width, UINT height);

    /**
     * @brief Update active camera state and process input callbacks.
     * @param dt Delta time in seconds since last update.
     *
     * This calls InputManager::Instance.ProcessCallbacks(dt) so registered input
     * callbacks (movement, mouse, toggle) are executed, and keeps camera per-frame
     * synchronization if necessary.
     */
    void Update(float dt);

    /**
     * @brief Returns position of the active camera.
    */
    DirectX::XMFLOAT3 GetActiveCameraPosition() const;

    /**
     * @brief Returns view-projection matrix of the active camera.
     */
    DirectX::XMMATRIX GetActiveViewProjection() const;

    /**
     * @brief Returns normalized forward vector of the active camera (world-space).
     */
    DirectX::XMFLOAT3 GetActiveCameraForward() const;

    /**
     * @brief Enable or disable camera input processing.
     * @param active If true, camera will respond to keyboard/mouse input.
     */
    void SetActive(bool active) { mIsActive = active; }

    /**
     * @brief Check if camera is currently processing input.
     */
    bool IsActive() const { return mIsActive; }
};