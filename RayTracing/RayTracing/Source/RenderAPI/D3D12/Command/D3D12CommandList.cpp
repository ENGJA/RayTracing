#include "pch.h"
#include "D3D12CommandList.h"
#include "helpers.h"

D3D12CommandList::~D3D12CommandList()
{
	Release();
}

void D3D12CommandList::Initialize(ID3D12Device* pDevice)
{
	HRESULT hr = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mCommandAllocator.GetAddressOf()));
	ASSERT_HR(hr, "Failed to create command allocator.");

	hr = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, mCommandAllocator.Get(), nullptr, IID_PPV_ARGS(&ptr_));
	ASSERT_HR(hr, "Failed to create command list.");

	hr = Get()->Close();
	ASSERT_HR(hr, "Failed to close command list after creation.");
}


void D3D12CommandList::Release()
{
	mCommandAllocator.Reset();
	Reset();
}
