#pragma once
#include "config.h"

class D3D12CommandList //: public Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>
{
private:
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;
	//Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
		//Microsoft::WRL::ComPtr<ID3D12Resource2> mBackBuffers[Config::cBufferCount];
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocators[Config::cBufferCount];
public:
	void Initialize(ID3D12Device* pDevice);
	ID3D12GraphicsCommandList* Get() const { return mCommandList.Get(); }
	void ResetCommandList(UINT frameIndex);
};

