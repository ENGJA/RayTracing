#pragma once
#include "D3D12RootSignature.h"
#include "RenderAPI/HLSL/HLSLShader.h"

class D3D12PipelineState
{
private:
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineState;
	D3D12RootSignature mRootSignature;
	HLSLShader mVertexShader;
	HLSLShader mPixelShader;

public:
	void Initialize(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc);


};

