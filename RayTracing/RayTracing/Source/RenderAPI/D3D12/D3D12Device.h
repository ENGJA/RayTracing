#pragma once

class D3D12Device //: public Microsoft::WRL::ComPtr<ID3D12Device5>
{
private:
	Microsoft::WRL::ComPtr<ID3D12Device5> mDevice;

public:
	void Initialize(IDXGIAdapter* pAdapter);
	ID3D12Device5* Get() const { return mDevice.Get(); }
};

