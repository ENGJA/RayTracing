#include "pch.h"
#include "InputManager.h"
#include <algorithm>

using namespace DirectX;

InputManager& InputManager::Instance = InputManager::get_instance();

void InputManager::Initialize(HWND hwnd)
{
	mHwnd = hwnd;
    // get initial mouse pos (client space)
    POINT p;
    if (GetCursorPos(&p))
    {
        ScreenToClient(mHwnd, &p);
        mLastMousePos = p;
        mHasLastPos = true;
    }

    SetCursorLocked(true);
}

void InputManager::BeginFrame()
{
    // reset per-frame deltas and flags before message dispatching
    mMouseDeltaX = 0.0f;
    mMouseDeltaY = 0.0f;
    mMouseWheelDelta = 0;
    std::fill(mKeyPressedThisFrame.begin(), mKeyPressedThisFrame.end(), false);
    std::fill(mKeyReleasedThisFrame.begin(), mKeyReleasedThisFrame.end(), false);
}

void InputManager::OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        int vkey = static_cast<int>(wParam) & 0xFF;
        if (vkey >= 0 && vkey < 256)
        {
            if (!mKeyDown[vkey])
                mKeyPressedThisFrame[vkey] = true;
            mKeyDown[vkey] = true;
        }
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int vkey = static_cast<int>(wParam) & 0xFF;
        if (vkey >= 0 && vkey < 256)
        {
            mKeyDown[vkey] = false;
            mKeyReleasedThisFrame[vkey] = true;
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        if (!mHwnd) break;
        if (mIsCursorLocked)
        {
            POINT p;
            p.x = (int)(short)LOWORD(lParam);
            p.y = (int)(short)HIWORD(lParam);

            // Calculate window center in Client Space
            RECT rect;
            GetClientRect(mHwnd, &rect);
            POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };

            // Calculate delta relative to center
            float dx = static_cast<float>(p.x - center.x);
            float dy = static_cast<float>(p.y - center.y);

            // If there is any movement
            if (dx != 0.0f || dy != 0.0f)
            {
                mMouseDeltaX += dx;
                mMouseDeltaY += dy;

                // Reset cursor to center (Physical Screen Space)
                POINT screenCenter = center;
                ClientToScreen(mHwnd, &screenCenter);
                SetCursorPos(screenCenter.x, screenCenter.y);

                // Update lastPos as center for next delta calculation
                mLastMousePos = center;
            }
        }
        else
        {
            // Standard handling (Menu Mode)
            POINT p;
            p.x = (int)(short)LOWORD(lParam);
            p.y = (int)(short)HIWORD(lParam);

            if (!mHasLastPos)
            {
                mLastMousePos = p;
                mHasLastPos = true;
            }
            mMouseDeltaX += static_cast<float>(p.x - mLastMousePos.x);
            mMouseDeltaY += static_cast<float>(p.y - mLastMousePos.y);
            mLastMousePos = p;
        }
        break;
    }
    case WM_MOUSEWHEEL:
    {
        SHORT delta = GET_WHEEL_DELTA_WPARAM(wParam);
        mMouseWheelDelta += static_cast<int>(delta);
        break;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        // also mark as key down (mouse buttons map to VK_LBUTTON etc.)
        // fallthrough to set mouse capture optionally
        // no extra handling here
        break;
    default:
        break;
    }
}

void InputManager::RegisterKeyPressedCallback(int vkey, KeyPressedCallback cb)
{
    mKeyPressedCallbacks[vkey].push_back(std::move(cb));
}

void InputManager::RegisterKeyDownCallback(int vkey, KeyDownCallback cb)
{
    mKeyDownCallbacks[vkey].push_back(std::move(cb));
}

void InputManager::RegisterMouseMoveCallback(MouseMoveCallback cb)
{
    mMouseMoveCallbacks.push_back(std::move(cb));
}

void InputManager::RegisterMouseWheelCallback(MouseWheelCallback cb)
{
    mMouseWheelCallbacks.push_back(std::move(cb));
}

void InputManager::ProcessCallbacks(float dt)
{
    // Edge callbacks: keys pressed this frame
    for (auto& pair : mKeyPressedCallbacks)
    {
        int vkey = pair.first;
        if (vkey < 0 || vkey >= 256) continue;
        if (mKeyPressedThisFrame[vkey])
        {
            for (auto& cb : pair.second)
            {
                if (cb) cb();
            }
        }
    }

    // Continuous callbacks: keys held down
    for (auto& pair : mKeyDownCallbacks)
    {
        int vkey = pair.first;
        if (vkey < 0 || vkey >= 256) continue;
        if (mKeyDown[vkey])
        {
            for (auto& cb : pair.second)
            {
                if (cb) cb(dt);
            }
        }
    }

    // Mouse move callbacks (pass accumulated delta)
    if (!mMouseMoveCallbacks.empty())
    {
        XMFLOAT2 delta(mMouseDeltaX, mMouseDeltaY);
        if (delta.x != 0.0f || delta.y != 0.0f)
        {
            for (auto& cb : mMouseMoveCallbacks)
            {
                if (cb) cb(delta);
            }
        }
    }

    // Mouse wheel callbacks
    if (!mMouseWheelCallbacks.empty() && mMouseWheelDelta != 0)
    {
        int wheel = mMouseWheelDelta;
        for (auto& cb : mMouseWheelCallbacks)
        {
            if (cb) cb(wheel);
        }
    }
}

void InputManager::SetCursorLocked(bool lock)
{
    if (mIsCursorLocked == lock) return;

    mIsCursorLocked = lock;

    if (mIsCursorLocked)
    {
        // Hide cursor
        while (ShowCursor(FALSE) >= 0);

        // Calculate window center to reset cursor there
        RECT rect;
        GetClientRect(mHwnd, &rect);
        POINT center = { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 };

        // Save center in screen-space
        POINT screenCenter = center;
        ClientToScreen(mHwnd, &screenCenter);
        mScreenCenter = screenCenter;

        // Set cursor to center and reset delta to avoid camera "jump"
        SetCursorPos(mScreenCenter.x, mScreenCenter.y);
        mLastMousePos = center;
        mHasLastPos = true;
    }
    else
    {
        // Show cursor
        while (ShowCursor(TRUE) < 0);
    }
}