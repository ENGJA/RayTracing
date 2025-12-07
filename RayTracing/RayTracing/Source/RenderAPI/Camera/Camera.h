#pragma once

#include <Windows.h>
#include <DirectXMath.h>

/**
 * @brief Camera type enumeration.
 */
enum class CameraType
{
    Fixed,  ///< Static camera with predefined view matrix
    Free    ///< Controllable camera with position and orientation
};

/**
 * @brief Camera parameters for initialization.
 */
struct CameraParams
{
    // Common parameters
    float fovY = 1.2217304764f;      ///< Vertical field of view in radians
    float aspect = 16.0f / 9.0f;     ///< Aspect ratio (width / height)
    float nearZ = 0.1f;              ///< Near clipping plane
    float farZ = 50.0f;              ///< Far clipping plane

    // Free camera parameters
    DirectX::XMFLOAT3 position{ 0.0f, 1.0f, -3.0f }; ///< Initial position
    float yaw = 0.0f;                ///< Initial yaw angle in radians
    float pitch = 0.0f;              ///< Initial pitch angle in radians

    // Fixed camera parameters
    DirectX::XMFLOAT3 lookFrom{ 0.0f, 0.0f, 0.0f };  ///< Camera position (for fixed)
    DirectX::XMFLOAT3 lookAt{ 0.0f, 0.0f, 1.0f };    ///< Look-at point (for fixed)
    DirectX::XMFLOAT3 up{ 0.0f, 1.0f, 0.0f };        ///< Up vector (for fixed)
};

/**
 * @brief Simple camera class for computing view and projection matrices.
 *
 * Supports two modes:
 *  - Fixed: Static camera with predefined view point
 *  - Free:  Controllable camera driven by input
 */
class Camera
{
private:
    CameraType mType = CameraType::Fixed;
    
    // Free-mode parameters
    DirectX::XMFLOAT3 mPosition{ 0.0f, 1.0f, -3.0f };
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    DirectX::XMFLOAT3 mWorldUp{ 0.0f, 1.0f, 0.0f };

    // Common parameters
    float mFovY = 1.2217304764f;
    float mAspect = 16.0f / 9.0f;
    float mNearZ = 0.1f;
    float mFarZ = 50.0f;

    // Cached matrices
    DirectX::XMMATRIX mFixedView{ DirectX::XMMatrixIdentity() };
    DirectX::XMMATRIX mProj{ DirectX::XMMatrixIdentity() };

public:
    Camera() = default;

    /**
     * @brief Initialize camera with specified type and parameters.
     * @param type Camera type (Fixed or Free)
     * @param params Camera parameters
     */
    void Initialize(CameraType type, const CameraParams& params);

    /**
     * @brief Returns combined view-projection matrix.
     */
    DirectX::XMMATRIX GetViewProjection() const;

    /**
     * @brief Returns current camera position in world space.
     */
    DirectX::XMFLOAT3 GetPosition() const;

    /**
     * @brief Returns forward/look direction in world space (normalized).
     */
    DirectX::XMFLOAT3 GetForward() const;

    /**
     * @brief Check if camera is in fixed mode.
     */
    bool IsFixed() const { return mType == CameraType::Fixed; }

    // Camera manipulation (only for Free camera)
    void AddYaw(float delta);
    void AddPitch(float delta);
    void MoveLocal(float forward, float right, float up);
    void ChangeFov(float delta);
};