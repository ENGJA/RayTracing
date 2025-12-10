#include "pch.h"
#include "config.h"
#include "D3D12PipelineState.h"
#include "helpers.h"
//#include "RenderAPI/HLSL/HLSLCompiler.h"

void D3D12PipelineState::InitializeOpaque(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided, DXGI_FORMAT rtvFormat)
{
	InitializeCommon(pDevice, std::move(vertexShader), std::move(pixelShader));
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = MakeBaseDesc(inputLayoutDesc, doubleSided, rtvFormat);

	// Specific changes for opaque
	gpsDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	gpsDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
	//gpsDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	HRESULT hr = pDevice->CreateGraphicsPipelineState(
		&gpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create pipeline state object.");
}

void D3D12PipelineState::InitializeTransparent(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc)
{
	InitializeCommon(pDevice, std::move(vertexShader), std::move(pixelShader));
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = MakeBaseDesc(inputLayoutDesc,  true);

	// Specific changes for transparent
	gpsDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	gpsDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	gpsDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	gpsDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	gpsDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	gpsDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	gpsDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	gpsDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	//gpsDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	HRESULT hr = pDevice->CreateGraphicsPipelineState(
		&gpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create pipeline state object.");
}

void D3D12PipelineState::InitializeCompute(ID3D12Device* pDevice, HLSLShader computeShader)
{
    // 1. Initialize Root Signature (Use the new Compute method)
    mRootSignature.InitializeCompute(pDevice);
    mComputeShader = std::move(computeShader);

    // 2. Create Compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
    computeDesc.pRootSignature = mRootSignature.Get();
    computeDesc.CS = { mComputeShader.GetShaderBlob()->GetBufferPointer(), mComputeShader.GetShaderBlob()->GetBufferSize() };
    
    HRESULT hr = pDevice->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf()));
    ASSERT_HR(hr, "Failed to create Compute PSO.");
}

void D3D12PipelineState::InitializeComposite(ID3D12Device* pDevice, HLSLShader computeShader)
{
	// 1. Initialize Root Signature (Use the new Compute method)
	mRootSignature.InitializeComposite(pDevice);
	mComputeShader = std::move(computeShader);
	// 2. Create Compute PSO
	D3D12_COMPUTE_PIPELINE_STATE_DESC computeDesc = {};
	computeDesc.pRootSignature = mRootSignature.Get();
	computeDesc.CS = { mComputeShader.GetShaderBlob()->GetBufferPointer(), mComputeShader.GetShaderBlob()->GetBufferSize() };
	
	HRESULT hr = pDevice->CreateComputePipelineState(&computeDesc, IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create Composite Compute PSO.");
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12PipelineState::MakeBaseDesc(const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided, DXGI_FORMAT rtvFormat) const
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc{};
	gpsDesc.pRootSignature = mRootSignature.Get();

	// Shaders
	gpsDesc.VS.pShaderBytecode = mVertexShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.VS.BytecodeLength = mVertexShader.GetShaderBlob()->GetBufferSize();
	gpsDesc.PS.pShaderBytecode = mPixelShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.PS.BytecodeLength = mPixelShader.GetShaderBlob()->GetBufferSize();

	// Depth-stencil
	gpsDesc.DepthStencilState.DepthEnable = TRUE;
	gpsDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	gpsDesc.DSVFormat = Config::cDepthBufferFormat;

	// Topology and render target formats
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = rtvFormat;

	// Rasterizer
	gpsDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gpsDesc.RasterizerState.CullMode = doubleSided ? D3D12_CULL_MODE_NONE : D3D12_CULL_MODE_BACK;

	// Blend
	gpsDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	// Sampling
	gpsDesc.SampleMask = UINT_MAX;
	gpsDesc.SampleDesc = { 1, 0 };

	// Input layout
	gpsDesc.InputLayout = inputLayoutDesc;

	return gpsDesc;
}

void D3D12PipelineState::InitializeCommon(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader)
{
	mRootSignature.Initialize(pDevice);
	mVertexShader = std::move(vertexShader);
	mPixelShader = std::move(pixelShader);
}
