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
        POINT p;
        p.x = (int)(short)LOWORD(lParam);
        p.y = (int)(short)HIWORD(lParam);
        // p is client coords
        if (!mHasLastPos)
        {
            mLastMousePos = p;
            mHasLastPos = true;
        }
        mMouseDeltaX += static_cast<float>(p.x - mLastMousePos.x);
        mMouseDeltaY += static_cast<float>(p.y - mLastMousePos.y);
        mLastMousePos = p;
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