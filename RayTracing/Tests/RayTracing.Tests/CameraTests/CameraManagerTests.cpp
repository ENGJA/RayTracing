#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/Camera/CameraManager.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests
{
	TEST_CLASS(CameraManagerTests)
	{
	public:
		
		TEST_METHOD(AddCamera_StoresCamera)
		{
			CameraManager manager;
			CameraParams params;
			params.position = { 0.0f, 0.0f, -5.0f };
			
			manager.AddCamera("Main", CameraType::Free, params);
			
			// By default, first camera might not be active unless set?
			// Let's check if we can set it active.
			manager.SetActiveCamera("Main");
			
			Camera* cam = manager.GetActiveCamera();
			Assert::IsNotNull(cam);
			
			XMFLOAT3 pos = cam->GetPosition();
			Assert::AreEqual(-5.0f, pos.z);
		}

		TEST_METHOD(SetActiveCamera_SwitchesCamera)
		{
			CameraManager manager;
			CameraParams params1;
			params1.position = { 0.0f, 0.0f, 1.0f };
			manager.AddCamera("Cam1", CameraType::Free, params1);

			CameraParams params2;
			params2.position = { 0.0f, 0.0f, 2.0f };
			manager.AddCamera("Cam2", CameraType::Free, params2);

			manager.SetActiveCamera("Cam1");
			Assert::AreEqual(1.0f, manager.GetActiveCamera()->GetPosition().z);

			manager.SetActiveCamera("Cam2");
			Assert::AreEqual(2.0f, manager.GetActiveCamera()->GetPosition().z);
		}
	};
}
