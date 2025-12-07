#pragma once
#include <Windows.h>
#include <deque>
#include <Pdh.h>
#include <PdhMsg.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#pragma comment(lib, "pdh.lib")

/**
 * @brief Monitors application performance metrics (FPS, CPU, GPU, RAM, VRAM).
 */
class PerformanceMonitor
{
public:
	/**
	 * @brief Initialize performance monitoring.
	 * @param adapter Optional DXGI adapter for VRAM monitoring.
	 * @return true on success, false otherwise.
	 */
	bool Initialize(IDXGIAdapter3* adapter = nullptr);

	/**
	 * @brief Cleanup performance monitoring resources.
	 */
	void Shutdown();

	/**
	 * @brief Update performance metrics. Call once per frame.
	 * @param deltaTime Time elapsed since last frame in seconds.
	 */
	void Update(float deltaTime);

	/**
	 * @brief Get current frames per second.
	 */
	float GetFPS() const { return mFPS; }

	/**
	 * @brief Get current CPU usage percentage (0-100).
	 */
	float GetCPUUsage() const { return mCPUUsage; }

	/**
	 * @brief Get current GPU usage percentage (0-100).
	 */
	float GetGPUUsage() const { return mGPUUsage; }

	/**
	 * @brief Get current RAM usage in MB.
	 */
	float GetRAMUsageMB() const { return mRAMUsageMB; }

	/**
	 * @brief Get current VRAM usage in MB.
	 */
	float GetVRAMUsageMB() const { return mVRAMUsageMB; }

	/**
	 * @brief Get average frame time in milliseconds.
	 */
	float GetAvgFrameTimeMS() const { return mAvgFrameTimeMS; }

private:
	void UpdateFPS(float deltaTime);
	void UpdateCPUUsage();
	void UpdateGPUUsage();
	void UpdateMemoryUsage();

	// FPS tracking
	float mFPS = 0.0f;
	float mFrameTimeAccumulator = 0.0f;
	int mFrameCount = 0;
	float mAvgFrameTimeMS = 0.0f;
	std::deque<float> mFrameTimes;
	static constexpr int MAX_FRAME_SAMPLES = 60;

	// CPU usage tracking
	PDH_HQUERY mCPUQuery = nullptr;
	PDH_HCOUNTER mCPUCounter = nullptr;
	float mCPUUsage = 0.0f;

	// GPU usage tracking
	PDH_HQUERY mGPUQuery = nullptr;
	PDH_HCOUNTER mGPUCounter = nullptr;
	float mGPUUsage = 0.0f;
	bool mGPUCounterAvailable = false;

	// Memory usage tracking
	float mRAMUsageMB = 0.0f;
	float mVRAMUsageMB = 0.0f;

	// DXGI adapter for VRAM queries
	Microsoft::WRL::ComPtr<IDXGIAdapter3> mAdapter;

	// Update intervals
	float mCPUUpdateTimer = 0.0f;
	float mGPUUpdateTimer = 0.0f;
	float mMemoryUpdateTimer = 0.0f;
	static constexpr float CPU_UPDATE_INTERVAL = 0.5f;
	static constexpr float GPU_UPDATE_INTERVAL = 0.5f;
	static constexpr float MEMORY_UPDATE_INTERVAL = 1.0f;
};
