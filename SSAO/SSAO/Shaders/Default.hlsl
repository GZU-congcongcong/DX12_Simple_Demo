///***********************************************************************************
/// 通用的不透明物体的着色器.
///***********************************************************************************

#include "Common.hlsl"

/// 顶点着色器输入.
struct VertexIn
{
	float3 PosL	    : POSITION;
	float3 NormalL  : NORMAL;
	float4 TangentL : TANGENT;
	float2 TexC     : TEXCOORD0;
};

/// 顶点着色器输出, 像素着色器输入.
struct VertexOut
{
	float4 PosH		 : SV_POSITION;
	float4 ShadowPos : POSITION0;
	float4 SSAOPosH  : POSITION1;
	float2 TexC		 : TEXCOORD0;

	// 在这里, TtoWorld.xyz存储切线、法线、副切线的xyz分量, w分量存储世界坐标.
	float4 TtoWorld0 : TEXCOORD1;
	float4 TtoWorld1 : TEXCOORD2;
	float4 TtoWorld2 : TEXCOORD3;
};


/// 顶点着色器.

VertexOut VS (VertexIn vin)
{
	VertexOut vout;

	float3 posW = mul(float4(vin.PosL, 1.0), gObjectConstants.gWorld).xyz;
	vout.PosH = mul(float4(posW, 1.0), gPassConstants.gViewProj);
	vout.ShadowPos = mul(float4(posW, 1.0), gPassConstants.gShadowTransform);
	vout.SSAOPosH = mul(float4(posW, 1.0), gPassConstants.gViewProjTex);

	MaterialData matData = gMaterialData[gObjectConstants.gMaterialIndex];
	float4 texC = mul(float4(vin.TexC, 0.0, 1.0), gObjectConstants.gTexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	// 这里法线变换需要注意, 这里是在等比变换的基础上, 因此不需要使用逆转置矩阵.
	float3 normalW = normalize(mul(vin.NormalL, (float3x3)gObjectConstants.gWorld));
	float3 tangentW = normalize(mul(vin.TangentL.xyz, (float3x3)gObjectConstants.gWorld));
	float3 binormal = cross(normalW, tangentW) * vin.TangentL.w;

	vout.TtoWorld0 = float4(tangentW, posW.x);
	vout.TtoWorld1 = float4(binormal, posW.y);
	vout.TtoWorld2 = float4(normalW, posW.z);

	return vout;
}

/// 像素着色器.
float4 PS(VertexOut vOut) : SV_Target
{
	//===============PBR====================
    MaterialData matData = gMaterialData[gObjectConstants.gMaterialIndex];
    float4 diffuseAlbedo = matData.DiffuseAlbedo;
    float3 fresnelR0 = matData.FresnelR0;
    float roughtness = matData.Roughness;
    uint diffuseIndex = matData.DiffuseMapIndex;
    uint normalIndex = matData.NormalMapIndex;
	
    diffuseAlbedo *= gTexMap[diffuseIndex].Sample(gsamAnisotropicWrap, vOut.TexC);
    float4 normalSampler = gTexMap[normalIndex].Sample(gsamAnisotropicWrap, vOut.TexC);
    
    //变换到世界坐标空间
    float3 NormalW = normalSampler.rgb * 2.0f - 1.0f;
    float3x3 TBNToWorld = float3x3(vOut.TtoWorld0.xyz, vOut.TtoWorld1.xyz, vOut.TtoWorld2.xyz);
    NormalW = normalize(mul(NormalW, TBNToWorld));
    
    float3 PosW = float3(vOut.TtoWorld0.w, vOut.TtoWorld1.w, vOut.TtoWorld2.w);
    float3 toEyePos = gPassConstants.gEyePosW - PosW;
    float dist = length(toEyePos);
    toEyePos /= dist;
	
    float4 ambient = gPassConstants.gAmbientLight;
	
	//环境光遮蔽
    vOut.SSAOPosH /= vOut.SSAOPosH.w;
    float ambientAccess = gSsaoMap.Sample(gsamLinearWrap, vOut.SSAOPosH.xy).r;
    ambient = ambient * diffuseAlbedo * ambientAccess; //
    
	//阴影因子
    float3 shadowFactor = float3(1.0, 1.0, 1.0);
    shadowFactor[0] = CalcShadowFactor(vOut.ShadowPos);
    
    const float metallic =  (1 - roughtness)* normalSampler.a;
    const float gShiness = (1 - roughtness) * normalSampler.a;
    
    float3 reflectDir = reflect(-toEyePos, NormalW);
    float3 reflectColor = gCubeMap.Sample(gsamAnisotropicWrap, reflectDir);
    
    ////////菲涅尔反射
    float3 fresnelFactor = SchlickFresnel(fresnelR0, NormalW, reflectDir);
    float4 fresnel = float4(gShiness * fresnelFactor * reflectColor.rgb, 1.0f);
	
    float4 L0 = PBR(NormalW, toEyePos, metallic, diffuseAlbedo.rgb, roughtness,
    1.0f, gPassConstants.gLights, PosW, shadowFactor);
    
    float3 Color = L0.xyz ;
	
    Color = Color / (Color + float3(1.0f, 1.0f, 1.0f));
    float gamma = 1.0 / 2.2;
    Color = pow(Color, gamma);
	
    float4 finalColor = float4(Color, 1.0f) + fresnel + ambient;
    finalColor.a = diffuseAlbedo.a;
	
    return finalColor;

}
