#pragma once
class D3D12CommandQueue : public Microsoft::WRL::ComPtr<ID3D12CommandQueue>
{
private:
	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;

	UINT64 mCurrentFenceValue = 0;

public:
	~D3D12CommandQueue();
	UINT64 GetCurrentFenceValue() const { return mCurrentFenceValue; }
	ID3D12Fence* GetFence() const { return mFence.Get(); }

	void Initialize(ID3D12Device* pDevice);
	void Release();
};

