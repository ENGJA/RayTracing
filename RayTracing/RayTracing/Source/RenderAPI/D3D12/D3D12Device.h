#pragma once

class D3D12Device : public Microsoft::WRL::ComPtr<ID3D12Device5>
{

public:
	void Initialize(IDXGIAdapter* pAdapter);
};

