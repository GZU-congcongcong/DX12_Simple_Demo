
#include"FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT InstanceCount, UINT MaterialCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdAlloc.GetAddressOf())));
	
	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, MaterialCount, false);
	InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, InstanceCount, false);


}