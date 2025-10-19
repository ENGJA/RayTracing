#include "pch.h"
#include "D3D12CommandList.h"
#include "helpers.h"

void D3D12CommandList::Initialize(ID3D12Device* pDevice)
{
	HRESULT hr;
	for (auto& allocator : mCommandAllocators)
	{
		hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.ReleaseAndGetAddressOf()));
		ASSERT_HR(hr, "Failed to create command allocator.");
	}

	hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocators[0].Get(), nullptr, IID_PPV_ARGS(mCommandList.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create command list.");

	hr = mCommandList->Close();
	ASSERT_HR(hr, "Failed to close command list after creation.");
}

void D3D12CommandList::ResetCommandList(UINT frameIndex)
{
	HRESULT hr = mCommandAllocators[frameIndex]->Reset();
	ASSERT_HR(hr, "Failed to reset command allocator.");

	hr = mCommandList->Reset(mCommandAllocators[frameIndex].Get(), nullptr);
	ASSERT_HR(hr, "Failed to reset command list.");
}

