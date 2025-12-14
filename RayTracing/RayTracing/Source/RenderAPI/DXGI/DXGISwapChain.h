#pragma once
#include "config.h"

/**
 * @brief Wrapper for DXGI swap chain and its back buffers.
 */
class DXGISwapChain
{
private:
	Microsoft::WRL::ComPtr<IDXGISwapChain4> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource2> mBackBuffers[Config::cBufferCount];
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr <ID3D12Device> mDevice;

	UINT mWidth = 0;
	UINT mHeight = 0;
	UINT mHeapIncrementSize = 0;

	/**
	 * @brief Creates the RTV descriptor heap.
	 */
	void CreateDescriptorHeap(ID3D12Device* pDevice);
	/**
	 * @brief Creates the swap chain tied to a window and command queue.
	 */
	void CreateSwapChain(IDXGIFactory2* pFactory, ID3D12CommandQueue* pCommandQueue, HWND hwnd);
	/**
	 * @brief Creates RTVs for back buffers.
	 */
	void CreateBufferViews();


public:
	/**
	 * @brief Initializes the swap chain and RTVs.
	 */
	void Initialize(IDXGIFactory2* pFactory, HWND hwnd, ID3D12CommandQueue* pCommandQueue, ID3D12Device* pDevice, UINT width, UINT height);
	/**
	 * @brief Presents the current back buffer.
	 */
	void Present();
	
	/**
	 * @brief Resizes the swap chain buffers and recreates RTVs.
	 * @param width New width.
	 * @param height New height.
	 */
	void Resize(UINT width, UINT height);
	
	/**
	 * @brief Index of the current back buffer.
	 */
	UINT GetCurrentBackBufferIndex() const { return mSwapChain->GetCurrentBackBufferIndex(); }
	/**
	 * @brief CPU descriptor for the current RTV.
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;
	/**
	 * @brief Returns the current back buffer resource.
	 */
	ID3D12Resource2* GetCurrentBackBuffer() const { return mBackBuffers[GetCurrentBackBufferIndex()].Get(); }

};

