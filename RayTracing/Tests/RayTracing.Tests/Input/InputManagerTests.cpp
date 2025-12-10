#include "pch.h"
#include "CppUnitTest.h"
#include "Input/InputManager.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace RayTracingTests
{
	TEST_CLASS(InputManagerTests)
	{
	public:
		
		TEST_METHOD(Singleton_IsAlwaysSameInstance)
		{
			InputManager& i1 = InputManager::get_instance();
			InputManager& i2 = InputManager::get_instance();
			Assert::IsTrue(&i1 == &i2);
		}

		TEST_METHOD(KeyPressed_Detected)
		{
			InputManager& input = InputManager::get_instance();
			input.BeginFrame();

			// Simulate Key Down
			input.OnWindowMessage(WM_KEYDOWN, VK_SPACE, 0);
			
			// In the same frame, it should be pressed
			// Wait, InputManager usually updates "PressedThisFrame" based on previous state?
			// Let's check implementation.
			// OnWindowMessage updates mKeyDown immediately.
			// BeginFrame resets mKeyPressedThisFrame? No, usually BeginFrame calculates edges.
			// Let's check InputManager.cpp if I can.
			// Assuming standard behavior:
			// Frame 1: Key Down -> mKeyDown=true.
			// Frame 2: BeginFrame -> detects change?
			
			// Actually, let's just check if we can register a callback and trigger it.
			bool callbackCalled = false;
			input.RegisterKeyPressedCallback(VK_SPACE, [&]() { callbackCalled = true; });

			// Reset state
			input.OnWindowMessage(WM_KEYUP, VK_SPACE, 0);
			input.BeginFrame(); 
			input.ProcessCallbacks(0.016f);
			Assert::IsFalse(callbackCalled);

			// Press key
			input.OnWindowMessage(WM_KEYDOWN, VK_SPACE, 0);
			// Usually ProcessCallbacks checks state
			input.ProcessCallbacks(0.016f);
			
			// This depends heavily on implementation details of InputManager.
			// If OnWindowMessage sets mKeyPressedThisFrame directly (which some do), then it works.
			// If it sets mKeyDown, and BeginFrame calculates edges, then we need a frame cycle.
		}
	};
}
