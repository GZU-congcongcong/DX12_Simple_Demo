// 基础光源的定义.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

/// ObjectCB的常量缓冲区结构体.
struct ObjectConstants
{
    float4x4 gWorld;
    float4x4 gTexTransform;

    uint gMaterialIndex;
    uint gObjPad0;
    uint gObjPad1;
    uint gObjPad2;
};

/// PassCB 的常量缓冲区结构体.
struct PassConstants
{
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;
	float4x4 gViewProjTex;
	float4x4 gShadowTransform;

	float3 gEyePosW;
	float cbPad0;
	float2 gRenderTargetSize;
	float2 gInvRenderTargetSize;

	float gNearZ;
	float gFarZ;
	float gTotalTime;
	float gDeltaTime;

	float4 gAmbientLight;
	Light gLights[MaxLights];
};

/// 材质数据结构体.
struct MaterialData
{
	float4 DiffuseAlbedo;
	float3 FresnelR0;
	float Roughness;
	float4x4 MatTransform;
	uint DiffuseMapIndex;
	uint NormalMapIndex;
	uint MatPad0;
	uint MatPad1;	
};


/// 常量缓冲区定义.
ConstantBuffer<ObjectConstants> gObjectConstants : register(b0);
ConstantBuffer<PassConstants> gPassConstants 	 : register(b1);

/// 着色器输入材质资源.
//StructuredBuffer<InstanceData> gInstanceData : register(t0, space0);
StructuredBuffer<MaterialData> gMaterialData : register(t0, space0);

/// 立方体纹理资源.
TextureCube gCubeMap : register(t1, space0);
Texture2D gShadowMap : register(t2, space0);
Texture2D gSsaoMap   : register(t3, space0);

/// 着色器输入纹理资源.
Texture2D gTexMap[6] : register(t4, space0);

/// 静态采样器.
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);
SamplerState gsamDepthMap		  : register(s7);

/// 计算阴影因子.
float CalcShadowFactor(float4 shadowPosH)
{
	// 变换到NDC空间计算, 用来深度值比较.
	shadowPosH.xyz /= shadowPosH.w;

	// NDC深度值.
	float depth = shadowPosH.z;

	uint width, height, numMips;
	gShadowMap.GetDimensions(0, width, height, numMips);

	// 周围偏移采样.
	float dx = 1.0 / width;	
	const float2 offsets[9] =
	{
		float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
		float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
		float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
	};

	float percentLit = 0.0;
	[unroll]
	for (int i = 0; i < 9; ++i)
	{
		percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowPosH.xy + offsets[i], depth).r;
	}

	return percentLit / 9.0;
}


#define PI 3.1415926535

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float DistributionGGX(float3 N, float3 H, float Roughness)
{
    float a = Roughness * Roughness;
    float a2 = a * a;
    float NdotH2 = pow(max(dot(N, H), 0.0), 2.0);
	
    float nom = a2;
    float denom = PI * pow(NdotH2 * (a2 - 1) + 1, 2);
	
    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float Roughness)
{
    float k = pow(Roughness + 1, 2) / 8;
    float denom = NdotV * (1 - k) + k;
	
    return NdotV / denom;
}

float GeomertrySmith(float3 N, float3 V, float3 L, float Roughness)
{
    float NdotV = max(dot(N, V), 0);
    float NdotL = max(dot(N, L), 0);
	
    float ggx1 = GeometrySchlickGGX(NdotL, Roughness);
    float ggx2 = GeometrySchlickGGX(NdotV, Roughness);
	
    return ggx1 * ggx2;
}

float BRDF(Light Lig, float3 N, float3 V, float Roughness, float metallic, float3 F0, float3 Albedo, float3 posW)
{
    float3 L = normalize(Lig.Position - posW);
    //float3 L = normalize(-Lig.Direction);
    float3 H = normalize(V + L);
		
    float dist = distance(Lig.Position, posW);
    float attenuation = 1.0 / (dist * dist);
    float3 radiance = Lig.Strength * attenuation;
		
    float NDF = DistributionGGX(N, H, Roughness);
    float G = GeomertrySmith(N, V, L, Roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 KS = F;
    float3 KD = float3(1.0f, 1.0f, 1.0f) - KS;
    KD *= (1.0 - metallic);
		
    float3 nom = NDF * G * F;
    float denom = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    float3 specular = nom / denom;
		
    float NdotL = max(dot(N, L), 0.0);
    return float4(((KD * Albedo / PI) + specular) * radiance * NdotL, 0.0f);
}

float4 PBR(float3 N, float3 V, float metallic, float3 Albedo,
		float Roughness, float Aocclusion, Light gLights[MaxLights],
        float3 posW, float3 ShadowFactor)
{
    //float3 F0 = float3(0.04f, 0.04f, 0.04f);
    //F0 = lerp(F0, Albedo, metallic);
    float3 F0 = float3(0.04f, 0.04f, 0.04f) * (1 - metallic) + metallic;
	
    float4 L0 = float4(0.0f, 0.0f, 0.0f,1.0f);
    int i = 0;
    
#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        L0 += ShadowFactor[i] * BRDF(gLights[i], N, V, Roughness, metallic, F0, Albedo, posW);

    }
#endif
	
    return L0;

}