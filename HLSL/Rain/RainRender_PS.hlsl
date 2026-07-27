#include "../Sampler.hlsli"
Texture2D gSceneDepth : register(t1); 


cbuffer RainCB : register(b0)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;

    float3 gCameraPos;
    float gDt;

    float3 gWind;
    float gTime;

    float3 gSpawnCenter;
    float gSpawnRadius;

    float gSpawnTopY;
    float gKillBottomY;
    float gGravity;
    float gWaterY;

    float2 gNearFar; 
    float gSoftRange; 
    float gRainIntensity;
    
    float3 gCameraVel;
    float gPad0;
};

struct PSIn
{
    float4 svpos : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 wpos : TEXCOORD1;
    float life : TEXCOORD2;
    float seed : TEXCOORD3;
};

float Hash11(float n)
{
    return frac(sin(n) * 43758.5453);
}


float3 ReconstructWorldPos(float2 uv, float depth01)
{
    float4 ndc = float4(uv.x * 2.0 - 1.0,
                        1.0 - uv.y * 2.0,
                        depth01,
                        1.0);
    float4 wp = mul(ndc, gInvViewProj);
    wp /= wp.w;
    return wp.xyz;
}

float4 main(PSIn pin) : SV_TARGET
{
    if (pin.life <= 0.0f)
        discard;

   
    float x = abs(pin.uv.x - 0.5) * 2.0;
    float core = saturate(1.0 - x);
    core = core * core;

    
    float tail = smoothstep(0.0, 0.35, pin.uv.y) * smoothstep(1.0, 0.65, pin.uv.y);

    float a = core * tail;

  
    float n = Hash11(pin.seed * 12.9898 + pin.uv.y * 78.233 + gTime * 0.15);
    a *= lerp(0.75, 1.25, n);

  
    float spark = smoothstep(0.985, 1.0, n);

    
    uint dw, dh;
    gSceneDepth.GetDimensions(dw, dh);
    float2 suv = pin.svpos.xy / float2((float) dw, (float) dh); 
    float sceneD = gSceneDepth.SampleLevel(sampler_states[ClampLinear], suv, 0).r;

    if (sceneD < 0.9999)
    {
        float3 sceneW = ReconstructWorldPos(suv, sceneD);
        float distToScene = length(sceneW - gCameraPos);
        float distToRain = length(pin.wpos - gCameraPos);

        float diff = distToScene - distToRain; 
        float soft = saturate(diff / max(gSoftRange, 0.001));
        a *= soft;
    }

    // ‰JF
    float3 col = float3(0.78, 0.82, 0.86);

    
    col += spark * 0.35;

    a *= gRainIntensity;

    
    return float4(col * a, a);
}
