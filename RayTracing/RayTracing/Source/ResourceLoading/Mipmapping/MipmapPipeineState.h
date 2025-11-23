#pragma once
#include "RenderAPI/HLSL/HLSLShader.h"
#include "ResourceLoading/Mipmapping/MipmapRootSignature.h"

/**
 * @brief Pipeline state object for mipmap generation compute shader.
 */
class MipmapPipeineState
{
private:
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineState;
	MipmapRootSignature mRootSignature;
	HLSLShader mComputeShader;

public:
	/**
	 * @brief Builds a PSO for the provided compute shader.
	 * @param pDevice D3D12 device.
	 * @param computeShader Compiled compute shader.
	 */
	void Initialize(ID3D12Device* pDevice, HLSLShader computeShader);
	/**
	 * @brief Returns the native root signature pointer.
	 */
	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	/**
	 * @brief Returns the native pipeline state pointer.
	 */
	ID3D12PipelineState* Get() const { return mPipelineState.Get(); }
	
};

