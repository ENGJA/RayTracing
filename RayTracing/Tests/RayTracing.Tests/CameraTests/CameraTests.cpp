#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/Camera.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests
{
	TEST_CLASS(CameraTests)
	{
	public:
		
		TEST_METHOD(Initialize_FreeCamera_SetsCorrectValues)
		{
			Camera camera;
			CameraParams params;
			params.position = { 0.0f, 0.0f, -5.0f };
			params.yaw = 0.0f;
			params.pitch = 0.0f;
			params.fovY = XM_PIDIV4;
			params.aspect = 1.777f;
			params.nearZ = 0.1f;
			params.farZ = 1000.0f;

			camera.Initialize(CameraType::Free, params);

			XMFLOAT3 pos = camera.GetPosition();
			Assert::AreEqual(0.0f, pos.x);
			Assert::AreEqual(0.0f, pos.y);
			Assert::AreEqual(-5.0f, pos.z);
		}

		TEST_METHOD(MoveLocal_Forward_UpdatesPosition)
		{
			Camera camera;
			CameraParams params;
			params.position = { 0.0f, 0.0f, 0.0f };
			params.yaw = 0.0f; 
			params.pitch = 0.0f;
			params.fovY = XM_PIDIV4;
			params.aspect = 1.6f;
			params.nearZ = 0.1f;
			params.farZ = 100.0f;

			camera.Initialize(CameraType::Free, params);

			// Move forward by 10 units
			// With yaw=0, pitch=0, forward direction depends on implementation.
			// In Camera.cpp: frontDir = (cp*cy, sp, cp*sy) = (1, 0, 0) -> +X is forward?
			// Wait, usually in LH systems:
			// Z is forward.
			// If yaw=0 means +Z, then cos(yaw) should be related to Z?
			// Standard math: x = cos(yaw)*cos(pitch), z = sin(yaw)*cos(pitch).
			// If yaw=0 -> x=1, z=0. So 0 degrees is +X.
			// If we want +Z to be forward, yaw should be 90 deg (PI/2).
			// Let's check if Camera.cpp does something different.
			// frontDir = XMVectorSet(cp * cy, sp, cp * sy, 0.0f);
			// Yes, this is standard spherical coordinates where 0 is +X.
			
			camera.MoveLocal(10.0f, 0.0f, 0.0f);

			XMFLOAT3 pos = camera.GetPosition();
			
			// We expect movement along X axis if yaw=0
			Assert::IsTrue(std::abs(pos.x - 10.0f) < 0.001f, L"Position X should be 10");
			Assert::AreEqual(0.0f, pos.y);
			Assert::AreEqual(0.0f, pos.z);
		}
	};
}
