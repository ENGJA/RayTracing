#include "pch.h"
#include "D3D12CommandQueue.h"
#include "helpers.h"


void D3D12CommandQueue::Initialize(ID3D12Device* pDevice)
{
	D3D12_COMMAND_QUEUE_DESC desc{};
	desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HRESULT hr = pDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&ptr_));
	ASSERT_HR(hr, "Failed to create command queue.");

	hr = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(mFence.GetAddressOf()));
	ASSERT_HR(hr, "Failed to create fence for command queue.");
}
