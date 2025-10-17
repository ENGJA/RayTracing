#pragma once

class D3D12CommandList : public Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>
{
private:
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocator;
public:
	void Initialize(ID3D12Device* pDevice);
};

