#pragma once

class D3D12Debug //: public Microsoft::WRL::ComPtr<ID3D12Debug6>
{
private:
	Microsoft::WRL::ComPtr<ID3D12Debug6> mDebug;
	static D3D12Debug mInstance;

	D3D12Debug() = default;
	bool EnsureInitialized();
	bool Initialize();
public:
	static D3D12Debug& GetInstance() { return mInstance; }
	void Enable();
};