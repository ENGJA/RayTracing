#include "pch.h"
#include "DXGISwapChain.h"
#include "helpers.h"

void DXGISwapChain::Initialize(IDXGIFactory2* pFactory, HWND hwnd, ID3D12CommandQueue* pCommandQueue, ID3D12Device* pDevice, UINT width, UINT height)
{
	mWidth = width;
	mHeight = height;
	mDevice = pDevice;
	mDevice->AddRef();

	CreateDescriptorHeap(pDevice);
	CreateSwapChain(pFactory, pCommandQueue, hwnd);
	CreateBufferViews();
}

void DXGISwapChain::Present()
{
	HRESULT hr = mSwapChain->Present(1, 0);
	CHECK_HR(hr, "Failed to present swap chain.");
}

D3D12_CPU_DESCRIPTOR_HANDLE DXGISwapChain::GetCurrentBackBufferView() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	rtvHandle.ptr += static_cast<SIZE_T>(GetCurrentBackBufferIndex()) * mHeapIncrementSize;
	return rtvHandle;
}

void DXGISwapChain::CreateDescriptorHeap(ID3D12Device* pDevice)
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = Config::cBufferCount;

	HRESULT hr = pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create RTV descriptor heap.");
	mHeapIncrementSize = pDevice->GetDescriptorHandleIncrementSize(rtvHeapDesc.Type);
}

void DXGISwapChain::CreateSwapChain(IDXGIFactory2* pFactory, ID3D12CommandQueue* pCommandQueue, HWND hwnd)
{
	Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain1;

	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = mWidth;
	swapChainDesc.Height = mHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.Stereo = FALSE;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = Config::cBufferCount;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;		// change if needed later
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;


	HRESULT hr = pFactory->CreateSwapChainForHwnd(
		pCommandQueue,
		hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		swapChain1.ReleaseAndGetAddressOf());

	ASSERT_HR(hr, "Failed to create swap chain.");
	hr = swapChain1.As(&mSwapChain);
	ASSERT_HR(hr, "Failed to cast IDXGISwapChain1 to IDXGISwapChain4.");
}

void DXGISwapChain::CreateBufferViews()
{
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < Config::cBufferCount; i++)
	{
		HRESULT hr = mSwapChain->GetBuffer(i, IID_PPV_ARGS(mBackBuffers[i].ReleaseAndGetAddressOf()));
		ASSERT_HR(hr, "Failed to get swap chain buffer at index " << i << ".");
		mDevice->CreateRenderTargetView(mBackBuffers[i].Get(), nullptr, rtvHandle);
		rtvHandle.ptr += mHeapIncrementSize;
	}
}




