#pragma once
class DXGISwapChain
{
private:
	constexpr static UINT cBufferCount = 2;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource2> mBackBuffers[cBufferCount];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr <ID3D12Device> mDevice;

	UINT mCurrentBackBufferIndex = 0;
	UINT mWidth = 0;
	UINT mHeight = 0;
	UINT mHeapIncrementSize = 0;

	void CreateDescriptorHeap(ID3D12Device* pDevice);
	void CreateSwapChain(IDXGIFactory2* pFactory, ID3D12CommandQueue* pCommandQueue, HWND hwnd);
	void CreateBufferViews();

public:
	void Initialize(IDXGIFactory2* pFactory, const HWND hwnd, ID3D12CommandQueue* pCommandQueue, ID3D12Device* pDevice, UINT width, UINT height);
	void Present();


};

