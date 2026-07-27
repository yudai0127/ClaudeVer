#include "SSR.hlsli" 

VS_OUT main(uint vid : SV_VertexID)
{
    float2 pos;
    if (vid == 0)
        pos = float2(-1.0, -1.0);
    else if (vid == 1)
        pos = float2(-1.0, 1.0);
    else if (vid == 2)
        pos = float2(1.0, -1.0);
    else
        pos = float2(1.0, 1.0);

    VS_OUT o;
    o.position = float4(pos, 0.0f, 1.0f);
    
  
    o.texcoord = pos * float2(0.5f, -0.5f) + 0.5f;
    
    o.color = float4(1.0f, 1.0f, 1.0f, 1.0f); 
    
    return o;
}