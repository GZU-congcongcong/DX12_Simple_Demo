
#include"Common.hlsl"

struct VertexIn
{
    float3 PosL : POSITION;
    float2 Tex : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_Position;
    float2 Tex : TEXCOORD;
};

VertexOut VS(VertexIn vIn)
{
    VertexOut vOut;
    
    vOut.PosH = float4(vIn.PosL, 1.0f);
    vOut.Tex = vIn.Tex;
    
    return vOut;
}

float4 PS(VertexOut vOut) : SV_Target
{
    return float4(gShadowMap.Sample(gsamAnisotropicWrap, vOut.Tex).rrr, 1.0f);
}

