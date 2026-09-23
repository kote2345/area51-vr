// Fullscreen triangle vertex shader for the multiview resolve.

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float2 UV       : TEXCOORD0;
    nointerpolation uint ViewID : TEXCOORD1;
};

VS_OUTPUT VSMain( uint VertexID : SV_VertexID, uint ViewIndex : SV_ViewID )
{
    VS_OUTPUT Output;
    float2 UV = float2( ( VertexID << 1 ) & 2, VertexID & 2 );
    Output.Position = float4( UV * float2( 2.0f, -2.0f ) +
                              float2( -1.0f, 1.0f ), 0.5f, 1.0f );
    Output.UV = UV;
    Output.ViewID = ViewIndex;
    return Output;
}
