#pragma once
/**
 * @brief Generic ID3D12Resource wrapper with convenience initializers.
 */
class D3D12Resource
{
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mResource;

public:

	ID3D12Resource** GetAddressOf() { return mResource.GetAddressOf(); }
	/**
	 * @brief Returns the native resource pointer.
	 */
	ID3D12Resource* Get() const { return mResource.Get(); }
	/**
	 * @brief Allocates a committed buffer resource of given size.
	 * @param pDevice D3D12 device.
	 * @param numBytes Size in bytes.
	 * @param heapType D3D12 heap type.
	 * @param initialState Initial resource state.
	 */
	void Initialize(ID3D12Device* pDevice, const unsigned int numBytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState);

	/**
	 * @brief Allocates a committed resource using a full resource descriptor.
	 * @param pDevice D3D12 device.
	 * @param resourceDesc Resource description.
	 * @param heapType D3D12 heap type.
	 * @param initialState Initial resource state.
	 * @param clearValue Optional clear value for certain resource types.
	 */
	void Initialize(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue = nullptr);


	/**
	* @brief Allocates a committed resource using a full resource descriptor and custom heap properties.
	* @param pDevice D3D12 device.
	* @param resourceDesc Resource description.
	* @param heapProps Custom heap properties.
	*/
	void Initialize(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_HEAP_PROPERTIES& heapProps);

	void Reset() { mResource.Reset(); }
};