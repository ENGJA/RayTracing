#pragma once
#include "D3D12RootSignature.h"
#include "RenderAPI/HLSL/HLSLShader.h"

/**
* @brief Wrapper for D3D12 pipeline state object (PSO) with associated shaders and root signature.
*/
class D3D12PipelineState
{
private:
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineState;
	D3D12RootSignature mRootSignature;
	HLSLShader mVertexShader;
	HLSLShader mPixelShader;
	HLSLShader mComputeShader;

	/**
	* @brief Creates a base graphics pipeline state description with common settings.
	* @param inputLayoutDesc Input layout description for vertex buffers.
	* @param doubleSided Whether to disable back-face culling.
	* @return Configured graphics pipeline state description.
	*/
	D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeBaseDesc(const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided, DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM) const;

	/**
	 * @brief Initializes common resources for the pipeline state.
	 * @param pDevice D3D12 device.
	 * @param vertexShader Compiled vertex shader.
	 * @param pixelShader Compiled pixel shader.
	 */
	void InitializeCommon(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader);

public:
	/**
	 * @brief Builds a PSO for the provided shaders and input layout.
	 * @param pDevice D3D12 device.
	 * @param vertexShader Compiled vertex shader.
	 * @param pixelShader Compiled pixel shader.
	 * @param inputLayoutDesc Input layout description for vertex buffers.
	 * @param doubleSided Whether to disable back-face culling.
	 */
	void InitializeOpaque(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc, bool doubleSided = false, DXGI_FORMAT rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM);

	/**
	 * @brief Builds a PSO for the provided shaders and input layout.
	 * @param pDevice D3D12 device.
	 * @param vertexShader Compiled vertex shader.
	 * @param pixelShader Compiled pixel shader.
	 * @param inputLayoutDesc Input layout description for vertex buffers.
	 */
	void InitializeTransparent(ID3D12Device* pDevice, HLSLShader vertexShader, HLSLShader pixelShader, const D3D12_INPUT_LAYOUT_DESC& inputLayoutDesc);



	void InitializeCompute(ID3D12Device* pDevice, HLSLShader computeShader);
	/**
	 * @brief Returns the native root signature pointer.
	 */
	ID3D12RootSignature* GetRootSignature() const { return mRootSignature.Get(); }
	/**
	 * @brief Returns the native pipeline state pointer.
	 */
	ID3D12PipelineState* Get() const { return mPipelineState.Get(); }
};

