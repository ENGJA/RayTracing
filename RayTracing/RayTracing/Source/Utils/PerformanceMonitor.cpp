#include "pch.h"
#include "PerformanceMonitor.h"
#include <Psapi.h>
#include <numeric>
#include <algorithm>

#pragma comment(lib, "psapi.lib")

bool PerformanceMonitor::Initialize(IDXGIAdapter3* adapter)
{
	// Store adapter for VRAM queries
	if (adapter)
	{
		mAdapter = adapter;
	}

	// Initialize CPU counter
	PDH_STATUS status = PdhOpenQuery(nullptr, 0, &mCPUQuery);
	if (status != ERROR_SUCCESS)
	{
		return false;
	}

	status = PdhAddEnglishCounter(mCPUQuery, L"\\Processor(_Total)\\% Processor Time", 0, &mCPUCounter);
	if (status != ERROR_SUCCESS)
	{
		PdhCloseQuery(mCPUQuery);
		mCPUQuery = nullptr;
		return false;
	}

	// Collect initial CPU data (first call returns 0)
	PdhCollectQueryData(mCPUQuery);

	// Try to initialize GPU counter (may not be available on all systems)
	status = PdhOpenQuery(nullptr, 0, &mGPUQuery);
	if (status == ERROR_SUCCESS)
	{
		// Try to find GPU engine counter - this may fail on some systems
		status = PdhAddEnglishCounter(mGPUQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &mGPUCounter);
		if (status == ERROR_SUCCESS)
		{
			PdhCollectQueryData(mGPUQuery);
			mGPUCounterAvailable = true;
		}
		else
		{
			PdhCloseQuery(mGPUQuery);
			mGPUQuery = nullptr;
			mGPUCounterAvailable = false;
		}
	}

	return true;
}

void PerformanceMonitor::Shutdown()
{
	if (mCPUQuery)
	{
		PdhCloseQuery(mCPUQuery);
		mCPUQuery = nullptr;
	}

	if (mGPUQuery)
	{
		PdhCloseQuery(mGPUQuery);
		mGPUQuery = nullptr;
	}
}

void PerformanceMonitor::Update(float deltaTime)
{
	UpdateFPS(deltaTime);

	mCPUUpdateTimer += deltaTime;
	if (mCPUUpdateTimer >= CPU_UPDATE_INTERVAL)
	{
		UpdateCPUUsage();
		mCPUUpdateTimer = 0.0f;
	}

	mGPUUpdateTimer += deltaTime;
	if (mGPUUpdateTimer >= GPU_UPDATE_INTERVAL)
	{
		UpdateGPUUsage();
		mGPUUpdateTimer = 0.0f;
	}

	mMemoryUpdateTimer += deltaTime;
	if (mMemoryUpdateTimer >= MEMORY_UPDATE_INTERVAL)
	{
		UpdateMemoryUsage();
		mMemoryUpdateTimer = 0.0f;
	}
}

void PerformanceMonitor::UpdateFPS(float deltaTime)
{
	mFrameCount++;
	mFrameTimeAccumulator += deltaTime;

	// Store frame time for averaging
	mFrameTimes.push_back(deltaTime);
	if (mFrameTimes.size() > MAX_FRAME_SAMPLES)
	{
		mFrameTimes.pop_front();
	}

	// Calculate average frame time
	if (!mFrameTimes.empty())
	{
		float sum = std::accumulate(mFrameTimes.begin(), mFrameTimes.end(), 0.0f);
		float avgFrameTime = sum / mFrameTimes.size();
		mAvgFrameTimeMS = avgFrameTime * 1000.0f;
	}

	// Update FPS every second
	if (mFrameTimeAccumulator >= 1.0f)
	{
		mFPS = static_cast<float>(mFrameCount) / mFrameTimeAccumulator;
		mFrameCount = 0;
		mFrameTimeAccumulator = 0.0f;
	}
}

void PerformanceMonitor::UpdateCPUUsage()
{
	if (!mCPUQuery)
		return;

	PDH_STATUS status = PdhCollectQueryData(mCPUQuery);
	if (status != ERROR_SUCCESS)
		return;

	PDH_FMT_COUNTERVALUE counterValue;
	status = PdhGetFormattedCounterValue(mCPUCounter, PDH_FMT_DOUBLE, nullptr, &counterValue);
	if (status == ERROR_SUCCESS)
	{
		mCPUUsage = static_cast<float>(counterValue.doubleValue);
	}
}

void PerformanceMonitor::UpdateGPUUsage()
{
	if (!mGPUCounterAvailable || !mGPUQuery)
	{
		mGPUUsage = 0.0f;
		return;
	}

	PDH_STATUS status = PdhCollectQueryData(mGPUQuery);
	if (status != ERROR_SUCCESS)
		return;

	// For GPU Engine counters, we need to enumerate all instances and sum them
	DWORD bufferSize = 0;
	DWORD itemCount = 0;
	status = PdhGetFormattedCounterArray(mGPUCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
	
	if (status == PDH_MORE_DATA && bufferSize > 0)
	{
		std::vector<BYTE> buffer(bufferSize);
		PDH_FMT_COUNTERVALUE_ITEM* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM*>(buffer.data());
		
		status = PdhGetFormattedCounterArray(mGPUCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);
		if (status == ERROR_SUCCESS)
		{
			double totalGPU = 0.0;
			for (DWORD i = 0; i < itemCount; i++)
			{
				totalGPU += items[i].FmtValue.doubleValue;
			}
			// Average across all GPU engines
			DWORD divisor = (itemCount > 0) ? itemCount : 1;
			mGPUUsage = static_cast<float>(totalGPU / divisor);
		}
	}
}

void PerformanceMonitor::UpdateMemoryUsage()
{
	// Get RAM usage for current process
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
	{
		mRAMUsageMB = static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
	}

	// Get VRAM usage from DXGI adapter
	if (mAdapter)
	{
		DXGI_QUERY_VIDEO_MEMORY_INFO memoryInfo;
		HRESULT hr = mAdapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memoryInfo);
		if (SUCCEEDED(hr))
		{
			mVRAMUsageMB = static_cast<float>(memoryInfo.CurrentUsage) / (1024.0f * 1024.0f);
		}
	}
}
