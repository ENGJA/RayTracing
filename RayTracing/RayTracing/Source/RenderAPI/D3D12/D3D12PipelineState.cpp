#include "pch.h"
#include "config.h"
#include "D3D12PipelineState.h"
#include "helpers.h"
//#include "RenderAPI/HLSL/HLSLCompiler.h"

void D3D12PipelineState::InitializeOpaque(ID3D12Device* pDevice, ID3D12RootSignature* rootSig, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided)
{
	InitializeCommon(pDevice, rootSig, std::move(vertexShader), std::move(pixelShader));
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = MakeBaseDesc(inputLayoutDesc, doubleSided);

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

void D3D12PipelineState::InitializeTransparent(ID3D12Device* pDevice, ID3D12RootSignature* rootSig, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc)
{
	InitializeCommon(pDevice, rootSig, std::move(vertexShader), std::move(pixelShader));
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc = MakeBaseDesc(inputLayoutDesc,  true);

	// Specific changes for transparent
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RTVFormats[1] = DXGI_FORMAT_UNKNOWN; // Clear others just to be safe
	gpsDesc.RTVFormats[2] = DXGI_FORMAT_UNKNOWN;
	gpsDesc.RTVFormats[3] = DXGI_FORMAT_UNKNOWN;

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

void D3D12PipelineState::InitializeCompute(ID3D12Device* pDevice, ID3D12RootSignature* rootSig, HLSLShader computeShader)
{
	mRootSignature = rootSig;
	mComputeShader = std::move(computeShader);
	D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = mRootSignature;
	desc.CS.pShaderBytecode = mComputeShader.GetShaderBlob()->GetBufferPointer();
	desc.CS.BytecodeLength = mComputeShader.GetShaderBlob()->GetBufferSize();
	desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	HRESULT hr = pDevice->CreateComputePipelineState(
		&desc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create compute pipeline state object.");
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC D3D12PipelineState::MakeBaseDesc(const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided) const
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc{};
	gpsDesc.pRootSignature = mRootSignature;

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
	gpsDesc.NumRenderTargets = 4;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;		// Albedo
	gpsDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Normals
	gpsDesc.RTVFormats[2] = DXGI_FORMAT_R32G32_FLOAT;		// Material properties
	gpsDesc.RTVFormats[3] = DXGI_FORMAT_R16G16B16A16_FLOAT; // Emissive

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

void D3D12PipelineState::InitializeCommon(ID3D12Device* pDevice, ID3D12RootSignature* rootSig, HLSLShader vertexShader, HLSLShader pixelShader)
{
	mRootSignature = rootSig;
	mVertexShader = std::move(vertexShader);
	mPixelShader = std::move(pixelShader);
}
