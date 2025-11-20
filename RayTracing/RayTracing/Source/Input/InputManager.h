#pragma once

#include <Windows.h>
#include <array>
#include <DirectXMath.h>

class InputManager
{
private:
    InputManager() = default; ///< Private ctor for singleton.

    HWND mHwnd = nullptr; ///< Window handle used for client-space mouse coordinates / capture.

    std::array<bool, 256> mKeyDown{};                 ///< Current key down state indexed by virtual-key.
    std::array<bool, 256> mKeyPressedThisFrame{};     ///< Keys pressed this frame (edge).
    std::array<bool, 256> mKeyReleasedThisFrame{};    ///< Keys released this frame (edge).

    POINT mLastMousePos{ 0,0 };    ///< Last known mouse position in client coordinates.
    float mMouseDeltaX = 0.0f;     ///< Accumulated mouse delta X since BeginFrame().
    float mMouseDeltaY = 0.0f;     ///< Accumulated mouse delta Y since BeginFrame().
    int mMouseWheelDelta = 0;      ///< Accumulated wheel delta (WHEEL_DELTA units) since BeginFrame().

    bool mHasLastPos = false; ///< True if mLastMousePos contains a valid previous position.

public:
    /**
     * @brief Returns the singleton instance.
     * @return Reference to global InputManager.
     */
    static InputManager& Get();

    /**
     * @brief Initialize the input manager with the application window handle.
     * @param hwnd Window handle used for client coordinates and optional capture.
     *
     * Call once during startup (e.g. Application::OnCreate) to let the manager
     * convert screen->client coordinates and optionally capture the cursor.
     */
    void Initialize(HWND hwnd);

    /**
     * @brief Feed Win32 window messages to the input manager.
     * @param msg Windows message code (WM_*).
     * @param wParam WPARAM from WindowProc.
     * @param lParam LPARAM from WindowProc.
     *
     * This method should be called from your WindowProc so the manager can
     * update keyboard and mouse state (WM_KEYDOWN/WM_KEYUP, WM_MOUSEMOVE, WM_MOUSEWHEEL, etc.).
     */
    void OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief Prepare input state for a new frame.
     *
     * Reset per-frame accumulators (mouse delta, wheel delta, pressed/released flags).
     * Call once at the start of each frame before message dispatching or polling.
     */
    void BeginFrame();

    /**
     * @brief Check whether a virtual-key is currently down.
     * @param vkey Virtual-key code (VK_*, ASCII letter, etc.).
     * @return true if the key is currently held down.
     */
    bool IsKeyDown(int vkey) const;

    /**
     * @brief Edge query: was the key pressed this frame?
     * @param vkey Virtual-key code.
     * @return true if the key transitioned from up->down during the current frame.
     */
    bool WasKeyPressed(int vkey) const;   // edge: pressed this frame

    /**
     * @brief Edge query: was the key released this frame?
     * @param vkey Virtual-key code.
     * @return true if the key transitioned from down->up during the current frame.
     */
    bool WasKeyReleased(int vkey) const;  // edge: released this frame

    /**
     * @brief Get accumulated mouse movement since BeginFrame().
     * @return XMFLOAT2 where x=deltaX, y=deltaY in client pixels.
     *
     * Mouse movement accumulates WM_MOUSEMOVE deltas. For high-resolution
     * raw input use WM_INPUT and adapt this manager accordingly.
     */
    DirectX::XMFLOAT2 GetMouseDelta() const; // delta since last frame (pixels)

    /**
     * @brief Get accumulated mouse wheel delta since BeginFrame().
     * @return Wheel delta in WHEEL_DELTA units (typically ±120 per notch).
     */
    int GetMouseWheelDelta() const;
};