#include "pch.h"
#include "InputManager.h"
#include <algorithm>

using namespace DirectX;

InputManager& InputManager::Get()
{
	static InputManager instance;
	return instance;
}

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

bool InputManager::IsKeyDown(int vkey) const
{
    if (vkey < 0 || vkey >= 256) return false;
    return mKeyDown[vkey];
}

bool InputManager::WasKeyPressed(int vkey) const
{
    if (vkey < 0 || vkey >= 256) return false;
    return mKeyPressedThisFrame[vkey];
}

bool InputManager::WasKeyReleased(int vkey) const
{
    if (vkey < 0 || vkey >= 256) return false;
    return mKeyReleasedThisFrame[vkey];
}

XMFLOAT2 InputManager::GetMouseDelta() const
{
    return XMFLOAT2(mMouseDeltaX, mMouseDeltaY);
}

int InputManager::GetMouseWheelDelta() const
{
    return mMouseWheelDelta;
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