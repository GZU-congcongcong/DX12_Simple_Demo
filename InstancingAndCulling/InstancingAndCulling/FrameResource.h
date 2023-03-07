
#ifndef FRAMERESOURCE_H_
#define FRAMERESOURCE_H_

#include"..\..\Common\UploadBuffer.h"
#include"..\..\Common\MathHelper.h"
#include"..\..\Common\d3dUtil.h"

using namespace DirectX;

struct Vertex
{
	XMFLOAT3 PosL;
	XMFLOAT3 NormalL;
	XMFLOAT2 Tex;
};

struct InstanceData
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	UINT MaterialIndex = 0;
	UINT pad0;
	UINT pad1;
	UINT pad2;
};

struct PassConstants
{
	XMFLOAT4X4 View = MathHelper::Identity4x4();
	XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();

	XMFLOAT3 EyePosW = { 1.0f,1.0f,1.0f };
	float pad;
	XMFLOAT2 RenderTargetSize = { 1.0f,1.0f };
	XMFLOAT2 InvRenderTargetSize = { 1.0f,1.0f };

	float NearZ;
	float FarZ;
	float TotalTime;
	float DeltaTime;

	XMFLOAT4 AmbientLight;
	Light Lights[MaxLights];
};

struct MaterialData
{
	XMFLOAT4 DiffuseAlbedo = { 1.0f,1.0f,1.0f,1.0f };
	XMFLOAT3 FresnelR0 = { 1.0f,1.0f,1.0f };
	float gRoughness;
	XMFLOAT4X4 MatTransform;

	UINT DiffuseMapIndex = 0;
	UINT pad0;
	UINT pad1; 
	UINT pad2;
};

struct FrameResource
{
	FrameResource(ID3D12Device* device, UINT passCount, UINT InstanceCount, UINT MaterialCount);
	FrameResource(const FrameResource& rhs) = delete;
	FrameResource& operator=(const FrameResource& rhs) = delete;
	~FrameResource() = default;

	Microsoft::WRL::ComPtr<ID3D12CommandAllocator>cmdAlloc;
	
	std::unique_ptr<UploadBuffer<PassConstants>>PassCB = nullptr;
	std::unique_ptr<UploadBuffer<MaterialData>>MaterialBuffer = nullptr;
	std::unique_ptr<UploadBuffer<InstanceData>>InstanceBuffer = nullptr;

	UINT64 Fence = 0;
};

#endif // !FRAMERESOURCE_H_
