
#ifndef INSTANCINGANDCULLINGAPP_H_
#define INSTANCINGANDCULLINGAPP_H_

#include"FrameResource.h"
#include"..\..\Common\Camera.h"
#include"..\..\Common\d3dApp.h"
#include"..\..\Common\d3dx12.h"
#include"..\..\Common\GeometryGenerator.h"
#include"..\..\Common\MathHelper.h"

using namespace DirectX;
using namespace DirectX::PackedVector;
using Microsoft::WRL::ComPtr;

const int gNumFrameResources = 3;

struct RenderItem
{
	RenderItem() = default;
	~RenderItem() = default;

	int NumFramesDirty = gNumFrameResources;

	int ObjectIndex = -1;

	XMFLOAT4X4 World = MathHelper::Identity4x4();
	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;
	D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	//包围盒、用于视锥体剔除
	BoundingBox Bounds;
	
	//需要实例化很多个同样的物体，因此用vector
	//同一个物体实例化需要不同的位置、纹理等信息
	std::vector<InstanceData>Instances;
	UINT InstanceCount = 0;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	UINT BaseVertexLocation = 0;
};

struct InstancingAndCullingApp :public D3DApp
{
public:
	InstancingAndCullingApp(HINSTANCE hInstance);
	InstancingAndCullingApp(const InstancingAndCullingApp& rhs) = delete;
	InstancingAndCullingApp& operator=(const InstancingAndCullingApp& rhs) = delete;
	~InstancingAndCullingApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState,int x,int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateInstanceData(const GameTimer& gt);
	void UpdateMaterialData(const GameTimer& gt);
	void UpdatePassCB(const GameTimer& gt);

	void LoadTextures();
	void BuildRootSignature();
	void BuildDescriptorHeap();
	void BuildShadersAndInputLayout();
	void BuildSkullGeometry();
	void BuildMaterials();
	void BuildRenderItems();
	void BuildFrameResources();
	void BuildPSO();

	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6>GetStaticSamplers();

private:
	std::vector<std::unique_ptr<FrameResource>>mFrameResources;
	FrameResource* mCurrFrameResource;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature>mRootSignature;
	ComPtr<ID3D12DescriptorHeap>mSrvDescrptorHeap;

	std::unordered_map<std::string, ComPtr<ID3DBlob>>mShaders;
	std::unordered_map <std::string, std::unique_ptr<Material>>mMaterials;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>>mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Texture>>mTextures;
	std::unordered_map < std::string, ComPtr<ID3D12PipelineState>>mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC>mInputLayout;
	std::vector < std::unique_ptr<RenderItem>>mAllitems;
	std::vector<RenderItem*>mOpaqueRitems;

	bool mFrustumCullingEnabled = true;
	BoundingFrustum mCamFrustum;

	PassConstants mMainPassCB;
	Camera mCamera;

	POINT mLastMousePos;
};

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		InstancingAndCullingApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}



#endif // !INSTANCINGANDCULLINGAPP_H_