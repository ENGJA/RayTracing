#include "pch.h"
#include "MipmapPipeineState.h"
#include "helpers.h"

void MipmapPipeineState::Initialize(ID3D12Device* pDevice, HLSLShader computeShader)
{
	mRootSignature.Initialize(pDevice);
	mComputeShader = std::move(computeShader);

	D3D12_COMPUTE_PIPELINE_STATE_DESC cpsDesc{};
	cpsDesc.pRootSignature = mRootSignature.Get();
	cpsDesc.CS.pShaderBytecode = mComputeShader.GetShaderBlob()->GetBufferPointer();
	cpsDesc.CS.BytecodeLength = mComputeShader.GetShaderBlob()->GetBufferSize();

	HRESULT hr = pDevice->CreateComputePipelineState(
		&cpsDesc,
		IID_PPV_ARGS(mPipelineState.ReleaseAndGetAddressOf())
	);

	ASSERT_HR(hr, "Failed to create compute pipeline state object.");
}
