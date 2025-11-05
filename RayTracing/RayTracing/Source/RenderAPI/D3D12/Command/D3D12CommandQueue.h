#pragma once
#include "config.h"

/**
 * @brief Wrapper for ID3D12CommandQueue and GPU synchronization fence logic.
 */
class D3D12CommandQueue
{
private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;

	UINT64 mFenceValues[Config::cBufferCount] = {};
	UINT64 mCurrentFenceValue = 0;
	HANDLE mFenceEvent = nullptr;

	/**
	 * @brief Signals the fence with the specified value.
	 */
	void SignalFence(UINT64 value);
	/**
	 * @brief Blocks CPU until the fence reaches the specified value.
	 */
	void WaitForFence(UINT64 value);
public:
	~D3D12CommandQueue();
	/**
	 * @brief Waits for all in-flight GPU work to complete.
	 */
	void Flush();
	/**
	 * @brief Returns the native command queue pointer.
	 */
	ID3D12CommandQueue* Get() const { return mCommandQueue.Get(); }

	/**
	 * @brief Creates the command queue and fence resources.
	 * @param pDevice D3D12 device.
	 */
	void Initialize(ID3D12Device* pDevice);
	/**
	 * @brief Submits an array of command lists to the queue for execution.
	 * @param numCommandLists Number of command lists.
	 * @param ppCommandLists Command list array.
	 */
	void ExecuteCommandLists(UINT numCommandLists, ID3D12CommandList* const* ppCommandLists);
	/**
	 * @brief Signals a per-frame fence value.
	 * @param frameIndex Current back buffer index.
	 */
	void SignalFenceInFrame(UINT frameIndex);
	/**
	 * @brief Waits on a per-frame fence value.
	 * @param frameIndex Current back buffer index.
	 */
	void WaitForFenceInFrame(UINT frameIndex);
};

