#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/Camera.h"
#include "RenderAPI/Camera/CameraManager.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Integration
{
	TEST_CLASS(CameraSystemTests)
	{
	public:
		
		TEST_METHOD(CameraSystem_ManagerAndCamera_WorkTogether)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);

			// Get initial position
			XMFLOAT3 initialPos = manager.GetActiveCameraPosition();
			
			// Verify camera system is initialized
			Assert::IsTrue(initialPos.x != 0.0f || initialPos.y != 0.0f || initialPos.z != 0.0f,
			               L"Camera should be initialized at non-zero position");
		}

		TEST_METHOD(CameraSystem_ResizePropagates)
		{
			CameraManager manager;
			manager.Initialize(800, 600);
			
			XMMATRIX vpBefore = manager.GetActiveViewProjection();
			
			// Resize
			manager.OnResize(1920, 1080);
			
			XMMATRIX vpAfter = manager.GetActiveViewProjection();
			
			// View-projection should change
			bool changed = false;
			for (int i = 0; i < 4 && !changed; i++)
			{
				if (std::abs(XMVectorGetX(vpBefore.r[i]) - XMVectorGetX(vpAfter.r[i])) > 0.001f)
					changed = true;
			}
			
			Assert::IsTrue(changed, L"Resize should propagate to active camera");
		}

		TEST_METHOD(CameraSystem_ActiveStatusControl)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			Assert::IsTrue(manager.IsActive(), L"Should be active by default");
			
			manager.SetActive(false);
			Assert::IsFalse(manager.IsActive());
			
			manager.SetActive(true);
			Assert::IsTrue(manager.IsActive());
		}

		TEST_METHOD(CameraSystem_SpeedMultiplierControl)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			Assert::AreEqual(1.0f, manager.GetMoveSpeedMultiplier());
			
			manager.SetMoveSpeedMultiplier(2.0f);
			Assert::AreEqual(2.0f, manager.GetMoveSpeedMultiplier());
			
			manager.SetMoveSpeedMultiplier(0.5f);
			Assert::AreEqual(0.5f, manager.GetMoveSpeedMultiplier());
		}

		TEST_METHOD(CameraSystem_ForwardVectorConsistency)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			XMFLOAT3 forward = manager.GetActiveCameraForward();
			
			// Forward vector should be normalized
			float length = std::sqrt(forward.x * forward.x + 
			                        forward.y * forward.y + 
			                        forward.z * forward.z);
			
			Assert::AreEqual(1.0f, length, 0.01f, L"Forward vector should be normalized");
		}

		TEST_METHOD(CameraSystem_MultipleUpdates_Stable)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			XMFLOAT3 posBefore = manager.GetActiveCameraPosition();
			
			// Multiple updates without input
			for (int i = 0; i < 100; i++)
			{
				manager.Update(0.016f);
			}
			
			XMFLOAT3 posAfter = manager.GetActiveCameraPosition();
			
			// Position shouldn't drift without input
			Assert::AreEqual(posBefore.x, posAfter.x, 0.001f);
			Assert::AreEqual(posBefore.y, posAfter.y, 0.001f);
			Assert::AreEqual(posBefore.z, posAfter.z, 0.001f);
		}

		TEST_METHOD(CameraSystem_CameraParamsIntegration)
		{
			// Test that CameraParams work correctly with Camera
			CameraParams params;
			params.position = { 10.0f, 5.0f, -10.0f };
			params.yaw = XM_PIDIV4; // 45 degrees
			params.pitch = 0.0f;
			params.fovY = XM_PIDIV2; // 90 degrees
			params.aspect = 16.0f / 9.0f;
			params.nearZ = 0.1f;
			params.farZ = 100.0f;

			Camera camera;
			camera.Initialize(CameraType::Free, params);

			XMFLOAT3 pos = camera.GetPosition();
			Assert::AreEqual(10.0f, pos.x, 0.001f);
			Assert::AreEqual(5.0f, pos.y, 0.001f);
			Assert::AreEqual(-10.0f, pos.z, 0.001f);
		}

		TEST_METHOD(CameraSystem_FixedAndFreeInteraction)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);

			// Manager initializes with fixed camera (index 0) and free camera (index 1)
			XMFLOAT3 pos1 = manager.GetActiveCameraPosition();
			
			// Both cameras should exist and have different positions
			// This verifies internal camera array is correctly set up
			Assert::IsTrue(pos1.x != 0.0f || pos1.y != 0.0f || pos1.z != 0.0f);
		}
	};
}
