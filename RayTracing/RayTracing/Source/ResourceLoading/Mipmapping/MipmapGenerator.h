#pragma once
#include "ResourceLoading/Mipmapping/MipmapPipeineState.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"

class MipmapGenerator
{
	MipmapPipeineState mPipelineState;

	ID3D12Device* mDevice;
	D3D12CommandList* mCommandList;
	D3D12CommandQueue* mCommandQueue;
	D3D12DescriptorHeap mDescriptorHeap;
	UINT mDescriptorSize;

	void InitializeDescriptorHeap();

public:
	void Initialize(ID3D12Device* pDevice, HLSLShader computeShader, D3D12CommandList* commandList, D3D12CommandQueue* commandQueue);
	void GenerateMipmaps(ID3D12Resource* textureResource, UINT width, UINT height, UINT mipLevels, UINT frameIndex);

};

