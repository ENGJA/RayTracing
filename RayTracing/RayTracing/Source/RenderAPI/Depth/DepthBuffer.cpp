#include "pch.h"
#include "config.h"
#include "DepthBuffer.h"

void DepthBuffer::Initialize(ID3D12Device* pDevice, UINT width, UINT height)
{
// 1. RESOURCE DESCRIPTION
    CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_TYPELESS,
        width, 
        height, 
        1, 1, 1, 0, 
        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL // Do NOT add DENY_SHADER_RESOURCE
    );

    // 2. CLEAR VALUE
	D3D12_CLEAR_VALUE clearValue{};
	clearValue.Format = Config::cDepthBufferFormat;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	// 3. CREATE RESOURCE
	mDepthStencilBuffer.Initialize(pDevice, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clearValue);

	// 4. CREATE DESCRIPTOR HEAP
	mDescHeap.Initialize(pDevice);

	// 5. CREATE DSV
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = Config::cDepthBufferFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	pDevice->CreateDepthStencilView(
		mDepthStencilBuffer.Get(),
		&dsvDesc,
		mDescHeap.GetDSVHandle()
	);
}
