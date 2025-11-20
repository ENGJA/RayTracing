#pragma once

#include <Windows.h>
#include <DirectXMath.h>

class InputManager;

/**
 * @brief Simple camera class for computing view and projection matrices.
 *
 * Supports two persistent modes:
 *  - fixed: uses a provided view matrix; no movement applied
 *  - free:  controllable camera (position, yaw/pitch, FOV) driven by input
 *
 * The camera exposes methods to initialize either mode, update free-mode state
 * from input each frame, and retrieve the combined view-projection matrix.
 */

class Camera
{
private:
    // free-mode parameters
    DirectX::XMFLOAT3 mPosition{ 0.0f, 1.0f, -3.0f }; ///< World-space camera position.
    float mYaw = 0.0f;   ///< Yaw angle (radians) — horizontal rotation around world up.
    float mPitch = 0.0f; ///< Pitch angle (radians) — vertical tilt (clamped to avoid flip).
    DirectX::XMFLOAT3 mWorldUp{ 0.0f, 1.0f, 0.0f }; ///< Up vector in world space.

    float mFovY = 1.2217304764f; ///< Vertical field of view in radians.
    float mAspect = 16.0f / 9.0f; ///< Aspect ratio (width / height).
    float mNearZ = 1.0f; ///< Near clipping plane.
    float mFarZ = 50.0f; ///< Far clipping plane.

    DirectX::XMMATRIX mFixedView{ DirectX::XMMatrixIdentity() }; ///< View matrix used in fixed mode.
    DirectX::XMMATRIX mProj{ DirectX::XMMatrixIdentity() }; ///< Projection matrix (kept in sync with FOV/aspect).

    bool mUseFixed = true;

public:
    Camera() = default;

    /**
    * @brief Initialize camera as fixed using precomputed view and projection matrices.
    * @param view  Precomputed view matrix (world -> view).
    * @param proj  Projection matrix (view -> clip).
    *
    * Sets the camera to persistent fixed mode. In this mode Update() is a no-op.
    */
    void InitializeFixed(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);

    /**
     * @brief Initialize camera as free (controllable).
     * @param pos    Initial world-space position.
     * @param yaw    Initial yaw angle in radians.
     * @param pitch  Initial pitch angle in radians.
     * @param fovY   Vertical field of view in radians.
     * @param aspect Aspect ratio (width / height).
     * @param nearZ  Near clipping plane.
     * @param farZ   Far clipping plane.
     *
     * Sets the camera to persistent free mode and initializes projection.
     */
    void InitializeFree(DirectX::XMFLOAT3 pos, float yaw, float pitch, float fovY, float aspect, float nearZ, float farZ);

    /**
     * @brief Update camera state from input for one frame.
     * @param dt    Delta time in seconds since last update.
     * @param input Reference to InputManager (keyboard/mouse state).
     *
     * In fixed mode this is a no-op. In free mode this updates yaw/pitch, position
     * and FOV according to input (movement keys, mouse delta, wheel, etc.).
     */
    void Update(float dt, const InputManager& input);

    /**
     * @brief Returns combined view-projection matrix depending on current mode.
     * @return XMMATRIX = view * proj
     *
     * If in fixed mode returns mFixedView * mProj. In free mode computes view
     * from position and yaw/pitch (using XMMatrixLookToLH) and multiplies by mProj.
     */
    DirectX::XMMATRIX GetViewProjection() const;
};