#include "pch.h"
#include "MipmapGenerator.h"


void MipmapGenerator::InitializeDescriptorHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = 64;// 2; // SRV + UAV per mip level, adjust as needed (max 32 mip levels supported here)
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	mDescriptorHeap.Initialize(mDevice, heapDesc);
	mDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(heapDesc.Type);
}

void MipmapGenerator::Initialize(ID3D12Device* pDevice, HLSLShader computeShader, D3D12CommandList* commandList, D3D12CommandQueue* commandQueue)
{
	mDevice = pDevice;
	mCommandList = commandList;
	mCommandQueue = commandQueue;
	mPipelineState.Initialize(pDevice, std::move(computeShader));
	InitializeDescriptorHeap();
}

void MipmapGenerator::GenerateMipmaps(ID3D12Resource* textureResource, UINT width, UINT height, UINT mipLevels, DXGI_FORMAT format, UINT frameIndex)
{
	mCommandList->ResetCommandList(frameIndex);
	ID3D12GraphicsCommandList* cmdList = mCommandList->Get();
	
	cmdList->SetPipelineState(mPipelineState.Get());
	cmdList->SetComputeRootSignature(mPipelineState.GetRootSignature());
	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
	cmdList->SetDescriptorHeaps(1, descriptorHeaps);


	{
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = textureResource;
		b.Transition.Subresource = 0;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // typically the state after upload and before generating mips
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		cmdList->ResourceBarrier(1, &b);
	}

	UINT srcWidth = width, srcHeight = height;

	for (UINT mip = 1; mip < mipLevels; mip++)
	{
		UINT dstWidth = std::max(1u, srcWidth >> 1);
		UINT dstHeight = std::max(1u, srcHeight >> 1);

		// Transition current mip (destination) to UAV
		{
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = textureResource;
			b.Transition.Subresource = mip;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; // if wasn't used
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			cmdList->ResourceBarrier(1, &b);
		}

		// Create / update descriptors
		UINT descriptorIndex = (mip - 1) * 2;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = mDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = mDescriptorHeap.Get()->GetGPUDescriptorHandleForHeapStart();	
		D3D12_CPU_DESCRIPTOR_HANDLE cpuSRV{ cpuStart.ptr + SIZE_T(descriptorIndex * mDescriptorSize) };
		D3D12_CPU_DESCRIPTOR_HANDLE cpuUAV{ cpuStart.ptr + SIZE_T((descriptorIndex + 1) * mDescriptorSize) };
		D3D12_GPU_DESCRIPTOR_HANDLE gpuSRV{ gpuStart.ptr + UINT64(descriptorIndex * mDescriptorSize) };
		D3D12_GPU_DESCRIPTOR_HANDLE gpuUAV{ gpuStart.ptr + UINT64((descriptorIndex + 1) * mDescriptorSize) };

		// SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Format = format;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MostDetailedMip = mip - 1;
		srv.Texture2D.MipLevels = 1;
		mDevice->CreateShaderResourceView(textureResource, &srv, cpuSRV);

		// UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = format;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uav.Texture2D.MipSlice = mip;
		mDevice->CreateUnorderedAccessView(textureResource, nullptr, &uav, cpuUAV);

		// Set root descriptors
		cmdList->SetComputeRootDescriptorTable(0, gpuSRV);
		cmdList->SetComputeRootDescriptorTable(1, gpuUAV);

		// Constants - mip size and level
		struct
		{
			UINT SrcSizeX;
			UINT SrcSizeY;
			UINT DstSizeX;
			UINT DstSizeY;
			UINT _Pad;
		} constants{ srcWidth, srcHeight, dstWidth, dstHeight, mip };

		cmdList->SetComputeRoot32BitConstants(2, 5, &constants, 0);

		UINT dispatchX = (dstWidth + 7) / 8;
		UINT dispatchY = (dstHeight + 7) / 8;
		cmdList->Dispatch(dispatchX, dispatchY, 1);

		// UAV barrier to ensure writes are visible
		{
			D3D12_RESOURCE_BARRIER uav{};
			uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
			uav.UAV.pResource = textureResource; // make it resource-specific
			cmdList->ResourceBarrier(1, &uav);
		}

		// Transition current mip from UAV to SRV or PIXEL_SHADER_RESOURCE for last mip
		{
			D3D12_RESOURCE_BARRIER b{};
			b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			b.Transition.pResource = textureResource;
			b.Transition.Subresource = mip;
			b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			b.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
			cmdList->ResourceBarrier(1, &b);
		}


		srcWidth = dstWidth;
		srcHeight = dstHeight;
	}

	// Transition entire resource to PIXEL_SHADER_RESOURCE state for sampling
	{
		D3D12_RESOURCE_BARRIER b{};
		b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition.pResource = textureResource;
		b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		b.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		cmdList->ResourceBarrier(1, &b);
	}

	// Close and execute
	cmdList->Close();
	ID3D12CommandList* lists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(1, lists);
	mCommandQueue->Flush();
}