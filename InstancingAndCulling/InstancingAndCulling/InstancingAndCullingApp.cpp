
#include"InstancingAndCullingApp.h"


InstancingAndCullingApp::InstancingAndCullingApp(HINSTANCE hInstance)
	:D3DApp(hInstance)
{

}

InstancingAndCullingApp::~InstancingAndCullingApp()
{

}

bool InstancingAndCullingApp::Initialize() {
	if (!D3DApp::Initialize())
		return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeap();
	BuildShadersAndInputLayout();
	BuildSkullGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPSO();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdlist[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdlist), cmdlist);
	FlushCommandQueue();

	return true;
}

void InstancingAndCullingApp::OnResize() {
	D3DApp::OnResize();
	mCamera.SetLens(0.25f * XM_PI, AspectRatio(), 1.0f, 1000.0f);

	//获取视锥体,这里是观察空间
	BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());
}

void InstancingAndCullingApp::Update(const GameTimer& gt) {

	OnKeyboardInput(gt);
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	if (mCurrFrameResource->Fence != 0 && mCurrFrameResource->Fence > mFence->GetCompletedValue()) {
		auto eventHadle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHadle));
		WaitForSingleObject(eventHadle, INFINITE);
		CloseHandle(eventHadle);
	}

	AnimateMaterials(gt);
	UpdateInstanceData(gt);
	UpdateMaterialData(gt);
	UpdatePassCB(gt);
}

void InstancingAndCullingApp::Draw(const GameTimer& gt) {
	auto cmdAlloc = mCurrFrameResource->cmdAlloc;
	ThrowIfFailed(cmdAlloc->Reset());
	ThrowIfFailed(mCommandList->Reset(cmdAlloc.Get(), mPSOs["Opaque"].Get()));

	mCommandList->RSSetScissorRects(1, &mScissorRect);
	mCommandList->RSSetViewports(1, &mScreenViewport);

	auto PreToRender=CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET);
	mCommandList->ResourceBarrier(1, &PreToRender);

	auto CBBV = CurrentBackBufferView();
	auto DSV = DepthStencilView();
	mCommandList->ClearDepthStencilView(DSV, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->ClearRenderTargetView(CBBV, Colors::LightSteelBlue, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &CBBV, true, &DSV);

	ID3D12DescriptorHeap* DescHeap[] = { mSrvDescrptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(DescHeap), DescHeap);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	mCommandList->SetGraphicsRootShaderResourceView(1,
		mCurrFrameResource->MaterialBuffer->Resource()->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootConstantBufferView(2,
		mCurrFrameResource->PassCB->Resource()->GetGPUVirtualAddress());
	mCommandList->SetGraphicsRootDescriptorTable(3,
		mSrvDescrptorHeap->GetGPUDescriptorHandleForHeapStart());

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

	auto RenderToPre = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	mCommandList->ResourceBarrier(1, &RenderToPre);

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdlist[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdlist), cmdlist);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void InstancingAndCullingApp::OnMouseDown(WPARAM btnState, int x, int y) {
	mLastMousePos.x = x;
	mLastMousePos.y = y;
	SetCapture(mhMainWnd);
}

void InstancingAndCullingApp::OnMouseUp(WPARAM btnState, int x, int y) {
	ReleaseCapture();
}

void InstancingAndCullingApp::OnMouseMove(WPARAM btnState, int x, int y) {
	if ((btnState && MK_LBUTTON) != 0) {
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}
	mLastMousePos.x = x;
	mLastMousePos.y = y;
}


void InstancingAndCullingApp::OnKeyboardInput(const GameTimer& gt) {
	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(20.0f * dt);
	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-20.0f * dt);
	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-20.0f * dt);
	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(20.0f * dt);

	if (GetAsyncKeyState('1') & 0x8000)
		mFrustumCullingEnabled = true;
	if (GetAsyncKeyState('2') & 0x8000)
		mFrustumCullingEnabled = false;

	mCamera.UpdateViewMatrix();
}

void InstancingAndCullingApp::AnimateMaterials(const GameTimer& gt) {

}

void InstancingAndCullingApp::UpdateInstanceData(const GameTimer& gt) {
	XMMATRIX view = mCamera.GetView();
	auto InvV = XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&InvV, view);

	auto currInstanceBuffer = mCurrFrameResource->InstanceBuffer.get();
	for (auto& e : mAllitems) {
		const auto& instantData = e->Instances;
		int VisableCount = 0;

		for (UINT i = 0; i < (UINT)instantData.size(); ++i) {
			XMMATRIX world = XMLoadFloat4x4(&instantData[i].World);
			XMMATRIX tesTransform = XMLoadFloat4x4(&instantData[i].TexTransform);
			
			auto InvW = XMMatrixDeterminant(world);
			XMMATRIX InvWorld = XMMatrixInverse(&InvW, world);
			XMMATRIX ViewToLoad = XMMatrixMultiply(invView, InvWorld);

			//视锥体创建就是在自己的观察空间的
			BoundingFrustum localSpaceFrustum;
			//先将自己的观察空间转到自己的世界空间，再转到物体的世界空间（局部空间）
			mCamFrustum.Transform(localSpaceFrustum, ViewToLoad);
			//现在这个视锥体应该是物体的视锥体
			if (localSpaceFrustum.Contains(e->Bounds) != DISJOINT)
			{
				InstanceData data;
				XMStoreFloat4x4(&data.World, XMMatrixTranspose(world));

				XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(tesTransform));

				data.MaterialIndex = instantData[i].MaterialIndex;

				//实例化多少个物体，就在缓冲区添加多少个数据
				currInstanceBuffer->CopyData(VisableCount++, data);
			}
		}
		e->InstanceCount = VisableCount;

		std::wostringstream outs;
		outs.precision(6);
		outs << L"Instancing and Culling Demo" <<
			L"    " << e->InstanceCount <<
			L" objects visible out of " << e->Instances.size();
		mMainWndCaption = outs.str();
	}

}

void InstancingAndCullingApp::UpdateMaterialData(const GameTimer& gt) {

	auto currmatBuff = mCurrFrameResource->MaterialBuffer.get();
	for (auto& e : mMaterials) {
		Material* mat = e.second.get();

		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;
			matData.FresnelR0 = mat->FresnelR0;
			matData.gRoughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));

			currmatBuff->CopyData(mat->MatCBIndex, matData);
			mat->NumFramesDirty--;
		}
	}
}

void InstancingAndCullingApp::UpdatePassCB(const GameTimer& gt) {
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	auto InvV = XMMatrixDeterminant(view);
	XMMATRIX invView = XMMatrixInverse(&InvV, view);
	auto InvP = XMMatrixDeterminant(proj);
	XMMATRIX invProj = XMMatrixInverse(&InvP, proj);
	auto InvVP = XMMatrixDeterminant(viewProj);
	XMMATRIX invViewProj = XMMatrixInverse(&InvVP, viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);

	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void InstancingAndCullingApp::LoadTextures() {
	const std::array<std::string, 7> texNameArr =
	{
		"bricksTex",
		"stoneTex",
		"tileTex",
		"crateTex",
		"iceTex",
		"grassTex",
		"defaultTex"
	};

	const std::array<std::wstring, 7> texFileNameArr =
	{
		L"../../Textures/bricks.dds",
		L"../../Textures/stone.dds",
		L"../../Textures/tile.dds",
		L"../../Textures/WoodCrate02.dds",
		L"../../Textures/ice.dds",
		L"../../Textures/grass.dds",
		L"../../Textures/white1x1.dds"
	};

	for (int i = 0; i < texNameArr.size(); ++i) {
		auto tex = std::make_unique<Texture>();
		tex->Name = texNameArr[i];
		tex->Filename = texFileNameArr[i];
		ThrowIfFailed(CreateDDSTextureFromFile12(md3dDevice.Get(),
			mCommandList.Get(), texFileNameArr[i].c_str(),
			tex->Resource, tex->UploadHeap));

		mTextures[tex->Name] = std::move(tex);
	}
}

void InstancingAndCullingApp::BuildRootSignature() {
	CD3DX12_DESCRIPTOR_RANGE SrvTable(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		7, 2,0);
	
	CD3DX12_ROOT_PARAMETER rootParam[4];
	rootParam[0].InitAsShaderResourceView(0, 0);	//InstanceData
	rootParam[1].InitAsShaderResourceView(1, 0);	//MaterialData
	rootParam[2].InitAsConstantBufferView(0, 0);	//PassCB
	rootParam[3].InitAsDescriptorTable(1, &SrvTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSample = GetStaticSamplers();
	CD3DX12_ROOT_SIGNATURE_DESC rootSig(4, rootParam,
		(UINT)staticSample.size(), staticSample.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob>SerializeBlob;
	ComPtr<ID3DBlob>ErrorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&rootSig,
		D3D_ROOT_SIGNATURE_VERSION_1, SerializeBlob.GetAddressOf(),
		ErrorBlob.GetAddressOf());

	if (ErrorBlob != nullptr)
		OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
	ThrowIfFailed(hr);
	
	ThrowIfFailed(md3dDevice->CreateRootSignature(0, SerializeBlob->GetBufferPointer(),
		SerializeBlob->GetBufferSize(), IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void InstancingAndCullingApp::BuildDescriptorHeap() {
	D3D12_DESCRIPTOR_HEAP_DESC SrvDesc;
	SrvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	SrvDesc.NodeMask = 0;
	SrvDesc.NumDescriptors = 7;
	SrvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&SrvDesc, IID_PPV_ARGS(mSrvDescrptorHeap.GetAddressOf())));

	std::vector<ComPtr<ID3D12Resource>>TexList =
	{
		mTextures["bricksTex"]->Resource,
		mTextures["stoneTex"]->Resource,
		mTextures["tileTex"]->Resource,
		mTextures["crateTex"]->Resource,
		mTextures["iceTex"]->Resource,
		mTextures["grassTex"]->Resource,
		mTextures["defaultTex"]->Resource
	};

	CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSrvDescrptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC TexDesc = {};
	TexDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	TexDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	TexDesc.Texture2D.MostDetailedMip = 0;
	TexDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	
	for (int i = 0; i < TexList.size(); ++i) {
		TexDesc.Texture2D.MipLevels = TexList[i]->GetDesc().MipLevels;
		TexDesc.Format = TexList[i]->GetDesc().Format;
		
		md3dDevice->CreateShaderResourceView(TexList[i].Get(),
			&TexDesc, handle);
		handle.Offset(1, mCbvSrvUavDescriptorSize);
	}

}

void InstancingAndCullingApp::BuildShadersAndInputLayout() {

	const D3D_SHADER_MACRO define[] =
	{
		"FOG","1",
		"NULL","NULL"
	};
	const D3D_SHADER_MACRO AlphaDefine[] =
	{
		"ALPHA_TEST","1",
		"FOG","1",
		"NULL","NULL"
	};
	
	mShaders["OpaqueVS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["OpaquePS"] = d3dUtil::CompileShader(L"Shaders/Default.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
		{"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,24,
		D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
	};
}

void InstancingAndCullingApp::BuildSkullGeometry() {
	std::ifstream fin("Models/car.txt");

	if (!fin) {
		MessageBox(0, L"Models/car.txt nor found", 0, 0);
		return;
	}

	UINT vCount = 0;
	UINT tCount = 0;
	std::string ignore;

	fin >> ignore >> vCount;
	fin >> ignore >> tCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
	XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
	XMVECTOR vMin = XMLoadFloat3(&vMinf3);
	XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

	std::vector<Vertex>vertices(vCount);
	for (UINT i = 0; i < vCount; ++i) {
		fin >> vertices[i].PosL.x >> vertices[i].PosL.y >> vertices[i].PosL.z;
		fin >> vertices[i].NormalL.x >> vertices[i].NormalL.y >> vertices[i].NormalL.z;
		
		//确定物体的包围盒，需要将物体转到局部坐标
		XMVECTOR pos = XMLoadFloat3(&vertices[i].PosL);

		vMin = XMVectorMin(vMin, pos);
		vMax = XMVectorMax(vMax, pos);

		//获得纹理坐标
		XMFLOAT3 spherePos;
		XMStoreFloat3(&spherePos, XMVector3Normalize(pos));

		float theta = atan2f(spherePos.z, spherePos.x);
		float phi = acosf(spherePos.y);
		if (theta < 0.0f)theta += XM_2PI;

		float u = theta / (2.0f * XM_PI);
		float v = phi / (XM_PI);

		vertices[i].Tex = { u,v };
	}

	BoundingBox bounds;
	XMStoreFloat3(&bounds.Center, 0.5f * (vMax + vMin));
	XMStoreFloat3(&bounds.Extents, 0.5f*(vMax - vMin));

	fin >> ignore >> ignore >> ignore;

	std::vector<std::uint32_t>indices(3 * tCount);
	for (UINT i = 0; i < tCount; ++i) {
		fin >> indices[i * 3] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}
	fin.close();

	const UINT verByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT indByteSize = (UINT)indices.size() * sizeof(std::uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "SkullGeo";

	ThrowIfFailed(D3DCreateBlob(verByteSize, geo->VertexBufferCPU.GetAddressOf()));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), verByteSize);
	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), verByteSize, geo->VertexBufferUploader);

	ThrowIfFailed(D3DCreateBlob(indByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), indByteSize);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), indByteSize, geo->IndexBufferUploader);

	SubmeshGeometry submesh;
	submesh.BaseVertexLocation = 0;
	submesh.Bounds = bounds;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;

	geo->IndexBufferByteSize = indByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->VertexBufferByteSize = verByteSize;
	geo->VertexByteStride = sizeof(Vertex);

	geo->DrawArgs["skull"] = submesh;
	mGeometries[geo->Name] = std::move(geo);
}

void InstancingAndCullingApp::BuildMaterials() {
	auto brick0 = std::make_unique<Material>();
	brick0->Name = "brick0";
	brick0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	brick0->DiffuseSrvHeapIndex = 0;
	brick0->MatCBIndex = 0;
	brick0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	brick0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->MatCBIndex = 1;
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->MatCBIndex = 2;
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;

	auto crate0 = std::make_unique<Material>();
	crate0->Name = "crate0";
	crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	crate0->DiffuseSrvHeapIndex = 3;
	crate0->MatCBIndex = 3;
	crate0->Roughness = 0.2f;
	crate0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);

	auto ice0 = std::make_unique<Material>();
	ice0->Name = "ice0";
	ice0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ice0->DiffuseSrvHeapIndex = 4;
	ice0->MatCBIndex = 4;
	ice0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	ice0->Roughness = 0.0f;

	auto grass0 = std::make_unique<Material>();
	grass0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass0->Name = "grass0";
	grass0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	grass0->DiffuseSrvHeapIndex = 5;
	grass0->MatCBIndex = 5;
	grass0->Roughness = 0.2f;

	auto skullmat = std::make_unique<Material>();
	skullmat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullmat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullmat->DiffuseSrvHeapIndex = 6;
	skullmat->MatCBIndex = 6;
	skullmat->Roughness = 0.5f;

	mMaterials["brick0"] = std::move(brick0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["crate0"] = std::move(crate0);
	mMaterials["ice0"] = std::move(ice0);
	mMaterials["grass0"] = std::move(grass0);
	mMaterials["skullmat"] = std::move(skullmat);
}

void InstancingAndCullingApp::BuildRenderItems() {
	auto skullRitems = std::make_unique<RenderItem>();
	skullRitems->Geo = mGeometries["SkullGeo"].get();
	skullRitems->IndexCount = skullRitems->Geo->DrawArgs["skull"].IndexCount;
	skullRitems->BaseVertexLocation = skullRitems->Geo->DrawArgs["skull"].BaseVertexLocation;
	skullRitems->StartIndexLocation = skullRitems->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitems->Bounds = skullRitems->Geo->DrawArgs["skull"].Bounds;
	skullRitems->Mat = mMaterials["tile0"].get();
	skullRitems->ObjectIndex = 0;
	skullRitems->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitems->InstanceCount = 0;

	//只有一个物体，但有125个实例
	const int n = 5;
	skullRitems->Instances.resize(n * n * n);

	float width = 200.0f;
	float height = 200.0f;
	float depth = 200.0f;
	
	//第一个实例的位置
	float x = -0.5f * width;
	float y = -0.5f * height;
	float z = -0.5f * depth;

	//距离多远就有一个实例
	float dx = width / (n - 1);
	float dy = height / (n - 1);
	float dz = depth / (n - 1);

	for (int i = 0; i < n; ++i) {				//y轴
		for (int j = 0; j < n; ++j) {			//z轴
			for (int k = 0; k < n; ++k) {		//x轴

				int index = i * n * n + j * n + k;

				skullRitems->Instances[index].World = XMFLOAT4X4(
					1.0f, 0.0f, 0.0f, 0.0f,
					0.0f, 1.0f, 0.0f, 0.0f,
					0.0f, 0.0f, 1.0f, 0.0f,
					x + k * dx, y + i * dy, z + j * dz, 1.0f
				);
				XMStoreFloat4x4(&skullRitems->TexTransform,
					XMMatrixScaling(2.0f, 2.0f, 1.0f));

				skullRitems->Instances[index].MaterialIndex = index % mMaterials.size();
			}
		}
	}
	mAllitems.push_back(std::move(skullRitems));

	for (auto& e : mAllitems)
		mOpaqueRitems.push_back(e.get());
}

void InstancingAndCullingApp::BuildFrameResources() {
	for (int i = 0; i < gNumFrameResources; ++i) {
		mFrameResources.push_back(std::make_unique<FrameResource>(
			md3dDevice.Get(), (UINT)mAllitems.size(), 1, mMaterials.size()));
	}
}

void InstancingAndCullingApp::BuildPSO() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueDesc;
	ZeroMemory(&opaqueDesc, sizeof(opaqueDesc));
	opaqueDesc.InputLayout = { mInputLayout.data(),(UINT)mInputLayout.size() };
	opaqueDesc.pRootSignature = mRootSignature.Get();
	opaqueDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["OpaqueVS"]->GetBufferPointer()),
		mShaders["OpaqueVS"]->GetBufferSize()
	};
	opaqueDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["OpaquePS"]->GetBufferPointer()),
		mShaders["OpaquePS"]->GetBufferSize()
	};
	opaqueDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaqueDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaqueDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaqueDesc.SampleDesc.Count = 1;
	opaqueDesc.SampleDesc.Quality = 0;
	opaqueDesc.SampleMask = UINT_MAX;
	opaqueDesc.NumRenderTargets = 1;
	opaqueDesc.RTVFormats[0] = mBackBufferFormat;
	opaqueDesc.DSVFormat = mDepthStencilFormat;
	opaqueDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueDesc,
		IID_PPV_ARGS(mPSOs["Opaque"].GetAddressOf())));
}

void InstancingAndCullingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {

	for (auto& e:ritems) {
		auto VB = e->Geo->VertexBufferView();
		auto IB = e->Geo->IndexBufferView();

		cmdList->IASetVertexBuffers(0, 1, &VB);
		cmdList->IASetIndexBuffer(&IB);
		cmdList->IASetPrimitiveTopology(e->primitiveType);

		auto instantBuffer = mCurrFrameResource->InstanceBuffer->Resource();

		cmdList->SetGraphicsRootShaderResourceView(0, instantBuffer->GetGPUVirtualAddress());
		cmdList->DrawIndexedInstanced(e->IndexCount, e->InstanceCount, 
			e->StartIndexLocation,e->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6>InstancingAndCullingApp::GetStaticSamplers() {
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	const CD3DX12_STATIC_SAMPLER_DESC anistropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	const CD3DX12_STATIC_SAMPLER_DESC anistropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	return { pointWrap, pointClamp, linearWrap, linearClamp, anistropicWrap, anistropicClamp };
}


