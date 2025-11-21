#pragma once

#include <Windows.h>
#include <array>
#include <DirectXMath.h>
#include <functional>

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

	using KeyPressedCallback = std::function<void()>; ///< Callback type for key-pressed (edge) events.
	using KeyDownCallback = std::function<void(float dt)>; ///< Callback type for key-down (continuous) events.

	std::unordered_map<int, std::vector<KeyPressedCallback>> mKeyPressedCallbacks; ///< Callbacks for key-pressed events.
	std::unordered_map<int, std::vector<KeyDownCallback>> mKeyDownCallbacks; ///< Callbacks for key-down events.

	using MouseMoveCallback = std::function<void(DirectX::XMFLOAT2 delta)>; ///< Callback type for mouse move events.
	using MouseWheelCallback = std::function<void(int wheelDelta)>; ///< Callback type for mouse wheel events.

	std::vector<MouseMoveCallback> mMouseMoveCallbacks; ///< Registered mouse move callbacks.
	std::vector<MouseWheelCallback> mMouseWheelCallbacks; ///< Registered mouse wheel callbacks.

public:
    /**
	* @brief Singleton instance of InputManager.
    */
    static InputManager& Instance;

    /**
	* @brief Returns the only instance of InputManager.
    */
    static InputManager& get_instance()
    {
        static InputManager inst;
        return inst;
    }

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
     * @brief Process registered callbacks.
     * @param dt Delta time for key-down (continuous) callbacks.
     *
     * Should be called once per frame (e.g. from CameraManager::Update).
     */
    void ProcessCallbacks(float dt);

    /**
    * @brief Register a callback invoked once when key is pressed (edge).
    * @param vkey Virtual-key code.
    * @param cb Callback invoked when key was pressed this frame.
    */
    void RegisterKeyPressedCallback(int vkey, KeyPressedCallback cb);

    /**
     * @brief Register a callback invoked every frame while key is down.
     * @param vkey Virtual-key code.
     * @param cb Callback invoked with dt while key is held.
     */
    void RegisterKeyDownCallback(int vkey, KeyDownCallback cb);


    /**
     * @brief Register a callback invoked every frame with current mouse delta.
     * @param cb Callback receiving accumulated mouse delta since BeginFrame().
     */
    void RegisterMouseMoveCallback(MouseMoveCallback cb);

    /**
     * @brief Register a callback invoked when mouse wheel delta is present (per ProcessCallbacks).
     * @param cb Callback receiving wheel delta in WHEEL_DELTA units.
     */
    void RegisterMouseWheelCallback(MouseWheelCallback cb);
};