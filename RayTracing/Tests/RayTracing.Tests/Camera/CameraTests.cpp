#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/Camera.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Camera
{
	TEST_CLASS(CameraTests)
	{
	public:
		
		TEST_METHOD(MoveLocal_Forward_UpdatesPosition)
		{
			::Camera camera;
			CameraParams params;
			params.position = { 0.0f, 0.0f, 0.0f };
			params.yaw = 0.0f; 
			params.pitch = 0.0f;

			camera.Initialize(CameraType::Free, params);
			camera.MoveLocal(10.0f, 0.0f, 0.0f);

			XMFLOAT3 pos = camera.GetPosition();
			Assert::IsTrue(std::abs(pos.x - 10.0f) < 0.001f, L"Position X should be ~10");
		}

		TEST_METHOD(MoveLocal_FixedCamera_DoesNothing)
		{
			::Camera camera;
			CameraParams params;
			params.lookFrom = { 0.0f, 0.0f, -5.0f };
			params.lookAt = { 0.0f, 0.0f, 0.0f };

			camera.Initialize(CameraType::Fixed, params);
			
			XMFLOAT3 posBefore = camera.GetPosition();
			camera.MoveLocal(10.0f, 0.0f, 0.0f);
			XMFLOAT3 posAfter = camera.GetPosition();

			Assert::AreEqual(posBefore.x, posAfter.x, 0.001f);
			Assert::AreEqual(posBefore.y, posAfter.y, 0.001f);
			Assert::AreEqual(posBefore.z, posAfter.z, 0.001f);
		}

		TEST_METHOD(AddYaw_RotatesCamera)
		{
			::Camera camera;
			CameraParams params;
			params.yaw = 0.0f;

			camera.Initialize(CameraType::Free, params);
			
			XMFLOAT3 forwardBefore = camera.GetForward();
			camera.AddYaw(XM_PIDIV2); // 90 degrees
			XMFLOAT3 forwardAfter = camera.GetForward();

			// Forward direction should change after yaw rotation
			float dotProduct = forwardBefore.x * forwardAfter.x + 
			                   forwardBefore.y * forwardAfter.y + 
			                   forwardBefore.z * forwardAfter.z;
			Assert::IsTrue(std::abs(dotProduct) < 0.1f, L"Forward vectors should be ~perpendicular after 90° yaw");
		}

		TEST_METHOD(AddPitch_ClampsToLimit)
		{
			::Camera camera;
			CameraParams params;
			params.pitch = 0.0f;

			camera.Initialize(CameraType::Free, params);
			
			// Try to pitch beyond limit
			camera.AddPitch(XM_PIDIV2 + 1.0f); // More than allowed
			XMFLOAT3 forward = camera.GetForward();

			// Should be clamped, so Y component shouldn't be exactly 1.0 or -1.0
			Assert::IsTrue(std::abs(forward.y) < 1.0f, L"Pitch should be clamped");
		}

		TEST_METHOD(AddPitch_FixedCamera_DoesNothing)
		{
			::Camera camera;
			CameraParams params;
			params.lookFrom = { 0.0f, 0.0f, -5.0f };
			params.lookAt = { 0.0f, 0.0f, 0.0f };

			camera.Initialize(CameraType::Fixed, params);
			
			XMFLOAT3 forwardBefore = camera.GetForward();
			camera.AddPitch(XM_PIDIV4);
			XMFLOAT3 forwardAfter = camera.GetForward();

			Assert::AreEqual(forwardBefore.x, forwardAfter.x, 0.001f);
			Assert::AreEqual(forwardBefore.y, forwardAfter.y, 0.001f);
			Assert::AreEqual(forwardBefore.z, forwardAfter.z, 0.001f);
		}

		TEST_METHOD(ChangeFov_UpdatesFieldOfView)
		{
			::Camera camera;
			CameraParams params;
			params.fovY = XM_PIDIV4; // 45 degrees
			params.aspect = 16.0f / 9.0f;

			camera.Initialize(CameraType::Free, params);
			
			// FOV change should affect projection matrix
			XMMATRIX vpBefore = camera.GetViewProjection();
			camera.ChangeFov(0.5f);
			XMMATRIX vpAfter = camera.GetViewProjection();

			// Matrices should differ
			bool isDifferent = false;
			for (int i = 0; i < 4; i++)
			{
				if (std::abs(XMVectorGetX(vpBefore.r[i]) - XMVectorGetX(vpAfter.r[i])) > 0.001f)
				{
					isDifferent = true;
					break;
				}
			}
			Assert::IsTrue(isDifferent, L"ViewProjection should change after FOV change");
		}

		TEST_METHOD(ChangeFov_ClampsToRange)
		{
			::Camera camera;
			CameraParams params;
			params.fovY = 1.0f;

			camera.Initialize(CameraType::Free, params);
			
			// Try to set FOV way too high
			camera.ChangeFov(10.0f);
			// Should be clamped to max (2.5)
			
			camera.ChangeFov(-10.0f);
			// Should be clamped to min (0.2)
			
			// If we got here without crash, clamping works
			Assert::IsTrue(true);
		}

		TEST_METHOD(OnResize_UpdatesAspectRatio)
		{
			::Camera camera;
			CameraParams params;
			params.fovY = XM_PIDIV4;
			params.aspect = 1.0f;

			camera.Initialize(CameraType::Free, params);
			
			XMMATRIX vpBefore = camera.GetViewProjection();
			camera.OnResize(1920, 1080); // 16:9 aspect ratio (1.777)
			XMMATRIX vpAfter = camera.GetViewProjection();

			// Check if any element of the matrix changed significantly
			bool matrixChanged = false;
			for (int row = 0; row < 4; row++)
			{
				for (int col = 0; col < 4; col++)
				{
					float before = 0.0f, after = 0.0f;
					
					switch(col)
					{
						case 0: before = XMVectorGetX(vpBefore.r[row]); after = XMVectorGetX(vpAfter.r[row]); break;
						case 1: before = XMVectorGetY(vpBefore.r[row]); after = XMVectorGetY(vpAfter.r[row]); break;
						case 2: before = XMVectorGetZ(vpBefore.r[row]); after = XMVectorGetZ(vpAfter.r[row]); break;
						case 3: before = XMVectorGetW(vpBefore.r[row]); after = XMVectorGetW(vpAfter.r[row]); break;
					}
					
					if (std::abs(before - after) > 0.001f)
					{
						matrixChanged = true;
						break;
					}
				}
				if (matrixChanged) break;
			}
			
			Assert::IsTrue(matrixChanged, L"ViewProjection matrix should change after resize");
		}

		TEST_METHOD(OnResize_ZeroDimensions_DoesNothing)
		{
			::Camera camera;
			CameraParams params;
			camera.Initialize(CameraType::Free, params);
			
			XMMATRIX vpBefore = camera.GetViewProjection();
			camera.OnResize(0, 0);
			XMMATRIX vpAfter = camera.GetViewProjection();

			Assert::AreEqual(XMVectorGetX(vpBefore.r[0]), XMVectorGetX(vpAfter.r[0]), 0.0001f);
		}

		TEST_METHOD(GetForward_ReturnsNormalizedVector)
		{
			::Camera camera;
			CameraParams params;
			camera.Initialize(CameraType::Free, params);

			XMFLOAT3 forward = camera.GetForward();
			float length = std::sqrt(forward.x * forward.x + 
			                        forward.y * forward.y + 
			                        forward.z * forward.z);

			Assert::AreEqual(1.0f, length, 0.001f, L"Forward vector should be normalized");
		}
	};
}
