#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/Camera.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Camera
{
	TEST_CLASS(CameraParamsTests)
	{
	public:
		
		TEST_METHOD(CameraParams_DefaultFovYIsValid)
		{
			CameraParams params;
			
			// Default FOV should be ~70 degrees (1.2217304764 radians)
			Assert::AreEqual(1.2217304764f, params.fovY, 0.0001f);
			Assert::IsTrue(params.fovY > 0.0f && params.fovY < XM_PI, L"FOV should be between 0 and PI");
		}

		TEST_METHOD(CameraParams_DefaultAspectIs16by9)
		{
			CameraParams params;
			
			float expected = 16.0f / 9.0f;
			Assert::AreEqual(expected, params.aspect, 0.001f);
		}

		TEST_METHOD(CameraParams_DefaultNearZIsPointOne)
		{
			CameraParams params;
			Assert::AreEqual(0.1f, params.nearZ);
		}

		TEST_METHOD(CameraParams_DefaultFarZIsFifty)
		{
			CameraParams params;
			Assert::AreEqual(50.0f, params.farZ);
		}

		TEST_METHOD(CameraParams_NearFarPlaneRelationship)
		{
			CameraParams params;
			Assert::IsTrue(params.nearZ < params.farZ, L"Near plane must be closer than far plane");
			Assert::IsTrue(params.nearZ > 0.0f, L"Near plane must be positive");
		}

		TEST_METHOD(CameraParams_DefaultPositionIsValid)
		{
			CameraParams params;
			
			Assert::AreEqual(0.0f, params.position.x);
			Assert::AreEqual(1.0f, params.position.y);
			Assert::AreEqual(-3.0f, params.position.z);
		}

		TEST_METHOD(CameraParams_DefaultYawIsZero)
		{
			CameraParams params;
			Assert::AreEqual(0.0f, params.yaw);
		}

		TEST_METHOD(CameraParams_DefaultPitchIsZero)
		{
			CameraParams params;
			Assert::AreEqual(0.0f, params.pitch);
		}

		TEST_METHOD(CameraParams_DefaultLookFromIsOrigin)
		{
			CameraParams params;
			
			Assert::AreEqual(0.0f, params.lookFrom.x);
			Assert::AreEqual(0.0f, params.lookFrom.y);
			Assert::AreEqual(0.0f, params.lookFrom.z);
		}

		TEST_METHOD(CameraParams_DefaultLookAtIsPlusZ)
		{
			CameraParams params;
			
			Assert::AreEqual(0.0f, params.lookAt.x);
			Assert::AreEqual(0.0f, params.lookAt.y);
			Assert::AreEqual(1.0f, params.lookAt.z);
		}

		TEST_METHOD(CameraParams_DefaultUpIsYAxis)
		{
			CameraParams params;
			
			Assert::AreEqual(0.0f, params.up.x);
			Assert::AreEqual(1.0f, params.up.y);
			Assert::AreEqual(0.0f, params.up.z);
		}

		TEST_METHOD(CameraParams_CanSetCustomValues)
		{
			CameraParams params;
			
			params.fovY = XM_PIDIV4; // 45 degrees
			params.aspect = 1.0f;
			params.nearZ = 0.5f;
			params.farZ = 1000.0f;
			params.position = { 10.0f, 20.0f, 30.0f };
			params.yaw = XM_PIDIV2; // 90 degrees
			params.pitch = XM_PIDIV4; // 45 degrees

			Assert::AreEqual(XM_PIDIV4, params.fovY);
			Assert::AreEqual(1.0f, params.aspect);
			Assert::AreEqual(0.5f, params.nearZ);
			Assert::AreEqual(1000.0f, params.farZ);
			Assert::AreEqual(10.0f, params.position.x);
			Assert::AreEqual(XM_PIDIV2, params.yaw);
		}

		TEST_METHOD(CameraParams_AspectRatioIsPositive)
		{
			CameraParams params;
			Assert::IsTrue(params.aspect > 0.0f, L"Aspect ratio must be positive");
		}

		TEST_METHOD(CameraParams_UpVectorIsNormalized)
		{
			CameraParams params;
			
			float length = std::sqrt(params.up.x * params.up.x + 
			                        params.up.y * params.up.y + 
			                        params.up.z * params.up.z);
			
			Assert::AreEqual(1.0f, length, 0.001f, L"Up vector should be normalized");
		}
	};
}
