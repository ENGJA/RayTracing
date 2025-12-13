#include "pch.h"
#include "CppUnitTest.h"
#include "Utils/PerformanceMonitor.h"
#include <thread>
#include <chrono>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace RayTracingTests::Utils
{
	TEST_CLASS(PerformanceMonitorTests)
	{
	public:
		
		TEST_METHOD(Initialize_ReturnsTrue)
		{
			PerformanceMonitor perf;
			bool result = perf.Initialize();
			Assert::IsTrue(result, L"Initialize should succeed");
			perf.Shutdown();
		}

		TEST_METHOD(Update_AccumulatesFPS)
		{
			PerformanceMonitor perf;
			perf.Initialize();
			
			// Simulate ~60 FPS for over 1 second
			for(int i = 0; i < 70; ++i)
			{
				perf.Update(0.016f);
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
			}

			float fps = perf.GetFPS();
			Assert::IsTrue(fps > 0.0f, L"FPS should be > 0 after multiple frames");
			Assert::IsTrue(fps < 200.0f, L"FPS should be realistic");
			
			perf.Shutdown();
		}

		TEST_METHOD(GetCPUUsage_ReturnsValidValue)
		{
			PerformanceMonitor perf;
			perf.Initialize();
			
			// Update a few times to get CPU reading
			for(int i = 0; i < 10; ++i)
			{
				perf.Update(0.016f);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}

			float cpu = perf.GetCPUUsage();
			Assert::IsTrue(cpu >= 0.0f && cpu <= 100.0f, L"CPU usage should be 0-100%");
			
			perf.Shutdown();
		}

		TEST_METHOD(GetRAMUsageMB_ReturnsPositive)
		{
			PerformanceMonitor perf;
			perf.Initialize();
			
			// Update to trigger memory query
			for(int i = 0; i < 5; ++i)
			{
				perf.Update(0.2f);
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}

			float ram = perf.GetRAMUsageMB();
			Assert::IsTrue(ram >= 0.0f, L"RAM usage should be non-negative");
			
			perf.Shutdown();
		}

		TEST_METHOD(MultipleInitialize_DoesNotCrash)
		{
			PerformanceMonitor perf;
			
			perf.Initialize();
			perf.Shutdown();
			
			perf.Initialize();
			perf.Update(0.016f);
			perf.Shutdown();
			
			Assert::IsTrue(true, L"Multiple init/shutdown cycles should work");
		}

		TEST_METHOD(RapidUpdates_DoesNotCrash)
		{
			PerformanceMonitor perf;
			perf.Initialize();
			
			// Very rapid updates
			for(int i = 0; i < 1000; ++i)
			{
				perf.Update(0.001f);
			}

			Assert::IsTrue(true, L"Should handle rapid updates");
			perf.Shutdown();
		}
	};
}
