// Fullscreen resolve for a two-layer VR color target.
#include "common/shader_bindings.hlsl"

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
    nointerpolation uint ViewID : TEXCOORD1;
};

A51_SAMPLED_TEXTURE_ATTR(0, 0) Texture2DArray CompositeSource A51_SAMPLED_TEXTURE_BIND(0, 0);
A51_SAMPLER_ATTR(0, 0) SamplerState samCompositeSource A51_SAMPLER_BIND(0, 0);

A51_CBUFFER_ATTR(1, 0) cbuffer CompositeParams A51_CBUFFER_BIND(1, 0)
{
    float4 BlendColor;
    int    BlendMode;
    float  Padding0;
    float  Padding1;
    float  Padding2;
};

float4 PSMain( VS_OUTPUT Input ) : SV_Target
{
    float4 Color = CompositeSource.SampleLevel( samCompositeSource,
                                                  float3( Input.UV, Input.ViewID ), 0.0f );
    return Color * BlendColor;
}
