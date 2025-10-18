#include "pch.h"
#include "D3D12CommandQueue.h"
#include "helpers.h"


D3D12CommandQueue::~D3D12CommandQueue()
{
	mCurrentFenceValue++;
	WaitForFence();

	if (mFenceEvent)
	{
		CloseHandle(mFenceEvent);
		mFenceEvent = nullptr;
	}
}

void D3D12CommandQueue::Initialize(ID3D12Device* pDevice)
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HRESULT hr = pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(mCommandQueue.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create command queue.");

	hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create fence for command queue.");

	mFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!mFenceEvent)
		ASSERT_HR(HRESULT_FROM_WIN32(GetLastError()), "Failed to create fence event handle for command queue.");
}

void D3D12CommandQueue::ExecuteCommandLists(UINT numCommandLists, ID3D12CommandList* const* ppCommandLists)
{
	mCommandQueue->ExecuteCommandLists(numCommandLists, ppCommandLists);
	mCurrentFenceValue++;
}

void D3D12CommandQueue::WaitForFence()
{
	HRESULT hr = mCommandQueue->Signal(mFence.Get(), mCurrentFenceValue);
	CHECK_HR(hr, "Failed to signal fence.");

	if (mFence->GetCompletedValue() < mCurrentFenceValue)
	{
		hr = mFence->SetEventOnCompletion(mCurrentFenceValue, mFenceEvent);
		CHECK_HR(hr, "Failed to set event on fence completion.");
		if (WaitForSingleObject(mFenceEvent, INFINITE) != WAIT_OBJECT_0)
			ASSERT_HR(HRESULT_FROM_WIN32(GetLastError()), "Failed to wait for fence event.");
	}
}
