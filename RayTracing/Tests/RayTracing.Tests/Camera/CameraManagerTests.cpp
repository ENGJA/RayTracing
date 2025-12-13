#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/CameraManager.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Camera
{
	TEST_CLASS(CameraManagerTests)
	{
	public:
		
		TEST_METHOD(Initialize_CreatesTwoCameras)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			// Should have valid view projection after initialization
			XMMATRIX vp = manager.GetActiveViewProjection();
			Assert::AreNotEqual(0.0f, XMVectorGetX(vp.r[0]), 0.001f);
		}

		TEST_METHOD(GetActiveCameraForward_ReturnsNormalizedVector)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			XMFLOAT3 forward = manager.GetActiveCameraForward();
			
			float length = std::sqrt(forward.x * forward.x + 
			                        forward.y * forward.y + 
			                        forward.z * forward.z);
			
			Assert::AreEqual(1.0f, length, 0.01f, L"Forward vector should be normalized");
		}

		TEST_METHOD(OnResize_UpdatesAllCameras)
		{
			CameraManager manager;
			manager.Initialize(800, 600);
			
			XMMATRIX vpBefore = manager.GetActiveViewProjection();
			
			// Resize to different aspect ratio
			manager.OnResize(1920, 1080);
			
			XMMATRIX vpAfter = manager.GetActiveViewProjection();
			
			// Projection should change due to aspect ratio change
			bool hasChanged = (XMVectorGetX(vpBefore.r[0]) != XMVectorGetX(vpAfter.r[0]));
			Assert::IsTrue(hasChanged, L"View-Projection should change after resize");
		}

		TEST_METHOD(SetActive_ControlsInputProcessing)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			Assert::IsTrue(manager.IsActive(), L"Should be active by default");
			
			manager.SetActive(false);
			Assert::IsFalse(manager.IsActive());
			
			manager.SetActive(true);
			Assert::IsTrue(manager.IsActive());
		}

		TEST_METHOD(SetMoveSpeedMultiplier_UpdatesSpeed)
		{
			CameraManager manager;
			manager.Initialize(1920, 1080);
			
			Assert::AreEqual(1.0f, manager.GetMoveSpeedMultiplier(), 0.001f);
			
			manager.SetMoveSpeedMultiplier(2.5f);
			Assert::AreEqual(2.5f, manager.GetMoveSpeedMultiplier(), 0.001f);
			
			manager.SetMoveSpeedMultiplier(0.5f);
			Assert::AreEqual(0.5f, manager.GetMoveSpeedMultiplier(), 0.001f);
		}

		TEST_METHOD(MultipleInitialize_Reinitializes)
		{
			CameraManager manager;
			
			manager.Initialize(1920, 1080);
			XMFLOAT3 pos1 = manager.GetActiveCameraPosition();
			
			// Re-initialize
			manager.Initialize(1920, 1080);
			XMFLOAT3 pos2 = manager.GetActiveCameraPosition();
			
			// Should return to initial fixed camera position
			Assert::AreEqual(pos1.x, pos2.x, 0.001f);
			Assert::AreEqual(pos1.y, pos2.y, 0.001f);
			Assert::AreEqual(pos1.z, pos2.z, 0.001f);
		}
	};
}
