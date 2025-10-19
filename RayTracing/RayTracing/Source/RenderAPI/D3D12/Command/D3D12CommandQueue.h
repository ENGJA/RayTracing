#pragma once
#include "config.h"
class D3D12CommandQueue //: public Microsoft::WRL::ComPtr<ID3D12CommandQueue>
{
private:
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;

	UINT64 mFenceValues[Config::cBufferCount] = {};
	UINT64 mCurrentFenceValue = 0;
	HANDLE mFenceEvent = nullptr;

	void SignalFence(UINT64 value);
	void WaitForFence(UINT64 value);
public:
	~D3D12CommandQueue();
	void Flush();
	//UINT64 GetCurrentFenceValue() const { return mCurrentFenceValue; }
	//ID3D12Fence* GetFence() const { return mFence.Get(); }
	ID3D12CommandQueue* Get() const { return mCommandQueue.Get(); }

	void Initialize(ID3D12Device* pDevice);
	void ExecuteCommandLists(UINT numCommandLists, ID3D12CommandList* const* ppCommandLists);
	void SignalFenceInFrame(UINT frameIndex);
	void WaitForFenceInFrame(UINT frameIndex);
};

