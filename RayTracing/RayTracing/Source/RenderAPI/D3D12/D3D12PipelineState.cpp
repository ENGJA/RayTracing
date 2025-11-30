#include "pch.h"
#include "config.h"
#include "D3D12PipelineState.h"
#include "helpers.h"
//#include "RenderAPI/HLSL/HLSLCompiler.h"

void D3D12PipelineState::InitializeOpaque(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc)
{
	mRootSignature.Initialize(pDevice);
	mVertexShader = std::move(vertexShader);
	mPixelShader = std::move(pixelShader);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc{};
	gpsDesc.pRootSignature = mRootSignature.Get();
	gpsDesc.VS.pShaderBytecode = mVertexShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.VS.BytecodeLength = mVertexShader.GetShaderBlob()->GetBufferSize();
	gpsDesc.PS.pShaderBytecode = mPixelShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.PS.BytecodeLength = mPixelShader.GetShaderBlob()->GetBufferSize();
	gpsDesc.DepthStencilState.DepthEnable = TRUE;
	gpsDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	gpsDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	gpsDesc.DSVFormat = Config::cDepthBufferFormat;
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gpsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	gpsDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	gpsDesc.SampleMask = UINT_MAX;
	gpsDesc.SampleDesc = { 1, 0 };
	gpsDesc.InputLayout = inputLayoutDesc;


	HRESULT hr = pDevice->CreateGraphicsPipelineState(
		&gpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create pipeline state object.");
}

void D3D12PipelineState::InitializeTransparent(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc)
{
	mRootSignature.Initialize(pDevice);
	mVertexShader = std::move(vertexShader);
	mPixelShader = std::move(pixelShader);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gpsDesc{};
	gpsDesc.pRootSignature = mRootSignature.Get();
	gpsDesc.VS.pShaderBytecode = mVertexShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.VS.BytecodeLength = mVertexShader.GetShaderBlob()->GetBufferSize();
	gpsDesc.PS.pShaderBytecode = mPixelShader.GetShaderBlob()->GetBufferPointer();
	gpsDesc.PS.BytecodeLength = mPixelShader.GetShaderBlob()->GetBufferSize();
	gpsDesc.DepthStencilState.DepthEnable = TRUE;
	gpsDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	gpsDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	gpsDesc.DSVFormat = Config::cDepthBufferFormat;
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gpsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	gpsDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	gpsDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	gpsDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	gpsDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	gpsDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	gpsDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	gpsDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	gpsDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	gpsDesc.SampleMask = UINT_MAX;
	gpsDesc.SampleDesc = { 1, 0 };
	gpsDesc.InputLayout = inputLayoutDesc;


	HRESULT hr = pDevice->CreateGraphicsPipelineState(
		&gpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create pipeline state object.");
}
