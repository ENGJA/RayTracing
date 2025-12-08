#pragma once
#include "RenderAPI/D3D12/D3D12RootSignature.h"
#include "RenderAPI/HLSL/HLSLShader.h"
#include "RenderAPI/D3D12/D3D12Resource.h"

class MeshGpuData;

class RayTracingPipeline
{
private:
    Microsoft::WRL::ComPtr<ID3D12StateObject> mStateObject;

    // The SBT Buffer (The most critical resource)
    D3D12Resource mSBTStorage;

    // Offsets into the SBT
    UINT mRayGenSectionSize = 0;
    UINT mMissSectionSize = 0;
    UINT mHitGroupSectionSize = 0;

    // Helper to calculate shader identifier size
    UINT GetShaderIdentifierSize(ID3D12Device5* pDevice);

public:
    void Initialize(ID3D12Device5* pDevice, D3D12RootSignature* globalSig, D3D12RootSignature* localSig, IDxcBlob* shaderBlob);

    /**
     * @brief Builds the SBT linking each mesh to its material textures.
     * This allows the shader to know "Mesh X uses Texture Y".
     */
    void BuildSBT(ID3D12Device5* pDevice, const std::vector<MeshGpuData>& meshes);

    void Dispatch(ID3D12GraphicsCommandList4* pCmd, UINT width, UINT height);
};

