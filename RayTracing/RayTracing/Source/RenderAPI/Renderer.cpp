#include "pch.h"

#include "D3D12/D3D12Debug.h"
#include "DataTypes.h"
#include "DXGI/DXGIDebug.h"
#include "DXGI/DXGIFactory.h"
#include "helpers.h"
#include "Renderer.h"


using std::wcout, std::endl;

void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
#ifdef _DEBUG
	D3D12Debug::GetInstance().Enable();
	DXGIDebug::GetInstance().Enable();
#endif

	DXGIFactory factory;
	DXGIAdapter adapter = factory.GetAdapter();

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = adapter->GetDesc(&desc);
	CHECK_HR(hr, "Failed to get adapter description.");

	wcout << "Selected device: " << desc.Description << endl;

	mDevice.Initialize(adapter.Get());
	mCommandQueue.Initialize(mDevice.Get());
	mCommandList.Initialize(mDevice.Get());
	mSwapChain.Initialize(factory.Get(), hwnd, mCommandQueue.Get(), mDevice.Get(), width, height);
	mWidth = width;
	mHeight = height;



	// temporary: create vertex buffer
	mVertexBuffer.Initialize(
		mDevice.Get(),
		sizeof(Vertex) * 3,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	mVertexBuffer.GetResource()->SetName(L"Vertex Buffer");

	const Vertex triangleVertices[] =
	{
		{ { 0.0f, 0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },      // Top vertex
		{ { 0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },     // Bottom right vertex
		{ { -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },    // Bottom left vertex
	};
	void* pData;
	hr = mVertexBuffer.GetResource()->Map(0, nullptr, &pData);
	ASSERT_HR(hr, "Failed to map vertex buffer.");
	memcpy(pData, triangleVertices, sizeof(triangleVertices));
	mVertexBuffer.GetResource()->Unmap(0, nullptr);
	// temporary end
}



void Renderer::Update()
{
	// Wait for GPU to finish with the current back buffer
	mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());

	// Open command list
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	// Initialize and set barrier
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = mSwapChain.GetCurrentBackBuffer();
	barrier.Transition.Subresource = 0;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	mCommandList.Get()->ResourceBarrier(1, &barrier);

	// Clear render target
	static int frameCount = 0;
	float red = sinf(frameCount++ * 0.01f) * 0.5f + 0.5f;
	const FLOAT clearColor[] = { red, 0.2f, 0.4f, 1.0f };

	mCommandList.Get()->ClearRenderTargetView(
		mSwapChain.GetCurrentBackBufferView(),
		clearColor,
		0,
		nullptr);



	// Change barrier states and set again
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	mCommandList.Get()->ResourceBarrier(1, &barrier);

	// Execute command list
	mCommandList.Get()->Close();
	ID3D12CommandList* commandLists[] = { mCommandList.Get() };
	mCommandQueue.ExecuteCommandLists(1, commandLists);

	// Present the frame
	mSwapChain.Present();

	// Signal and increment the fence value
	mCommandQueue.SignalFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
}

