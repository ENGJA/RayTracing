#include "pch.h"
#include "D3D12PipelineState.h"
#include "helpers.h"
//#include "RenderAPI/HLSL/HLSLCompiler.h"

void D3D12PipelineState::Initialize(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc)
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
	gpsDesc.DepthStencilState.DepthEnable = FALSE; // until depth buffer is implemented
	gpsDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gpsDesc.NumRenderTargets = 1;
	gpsDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	gpsDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	gpsDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
	gpsDesc.SampleMask = UINT_MAX;
	gpsDesc.SampleDesc = { 1, 0 };
	gpsDesc.InputLayout = inputLayoutDesc;


	HRESULT hr = pDevice->CreateGraphicsPipelineState(
		&gpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create pipeline state object.");
}
