#pragma once
#include "RenderAPI/D3D12/D3D12Device.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "RenderAPI/D3D12/D3D12Resource.h"

struct MeshGpuData;

struct BlasBuildReq
{
    MeshGpuData* mesh;
    D3D12_RAYTRACING_GEOMETRY_DESC geomDesc;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
};

class RayTracingBuilder
{
private:
    ID3D12Device5* mDevice = nullptr;
    ID3D12GraphicsCommandList4* mCmdList = nullptr; // Note: DXR requires CommandList4 or higher
    D3D12CommandQueue* mQueue = nullptr;

	std::vector<D3D12Resource> mTempResources; ///< Temporary scratch buffers to be cleared after TLAS build

public:
    /**
	* @brief Initializes the ray tracing builder with device and command list.
	* @param pDevice D3D12 device with ray tracing support.
	* @param pCmdList D3D12 graphics command list for recording build commands.
	* @param pQueue D3D12 command queue for executing commands.
    */
    void Initialize(ID3D12Device5* pDevice, ID3D12GraphicsCommandList4* pCmdList, D3D12CommandQueue* pQueue);

    
    /**
	* @brief Builds bottom-level acceleration structures (BLAS) for all meshes, categorized by material type.
	* @param opaqueSingle list of opaque single-sided meshes.
	* @param opaqueDouble list of opaque double-sided meshes.
	* @param maskedSingle list of masked single-sided meshes.
	* @param maskedDouble list of masked double-sided meshes.
	* @param transparent list of transparent meshes.
    */
    void BuildAllBLAS(
        std::vector<MeshGpuData>& opaqueSingle,
        std::vector<MeshGpuData>& opaqueDouble,
        std::vector<MeshGpuData>& maskedSingle,
        std::vector<MeshGpuData>& maskedDouble,
        std::vector<MeshGpuData>& transparentSignle,
        std::vector<MeshGpuData>& transparentDouble);


	/**
	* @brief Builds the top-level acceleration structure (TLAS) from categorized mesh lists.
	* @param opaqueSingle list of opaque single-sided meshes.
	* @param opaqueDouble list of opaque double-sided meshes.
	* @param maskedSingle list of masked single-sided meshes.
	* @param maskedDouble list of masked double-sided meshes.
	* @param transparent list of transparent meshes.
	* @param tlasResultBuffer Output buffer for the TLAS result.
	* @param tlasScratchBuffer Scratch buffer for TLAS construction.
	* @param instanceDescsBuffer Buffer containing instance descriptions for TLAS build.
    */
    void BuildTLAS(
        const std::vector<MeshGpuData>& opaqueSingle, 
        const std::vector<MeshGpuData>& opaqueDouble, 
        const std::vector<MeshGpuData>& maskedSingle, 
        const std::vector<MeshGpuData>& maskedDouble,
        const std::vector<MeshGpuData>& transparentSingle,
        const std::vector<MeshGpuData>& transparentDouble,
        D3D12Resource& tlasResultBuffer, 
        D3D12Resource& tlasScratchBuffer, 
        D3D12Resource& instanceDescsBuffer);


    void BuildSingleGlobalBLAS(
        std::vector<MeshGpuData>& opaqueSingle,
        std::vector<MeshGpuData>& opaqueDouble,
        std::vector<MeshGpuData>& maskedSingle,
        std::vector<MeshGpuData>& maskedDouble,
        std::vector<MeshGpuData>& transparentSingle,
        std::vector<MeshGpuData>& transparentDouble,
        D3D12Resource& outBlasResult);


    void BuildSingleGlobalTLAS(
        const D3D12Resource& unifiedBlas,
        UINT totalGeomCount,
        D3D12Resource& tlasResultBuffer,
        D3D12Resource& tlasScratchBuffer,
        D3D12Resource& instanceDescsBuffer);

    /**
	* @brief Clears temporary scratch resources used during acceleration structure builds.
    */
    void ClearScratchResources() { mTempResources = {}; }

};

