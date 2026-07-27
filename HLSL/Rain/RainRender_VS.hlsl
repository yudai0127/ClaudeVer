struct Particle
{
    float3 pos;
    float life;
    float3 vel;
    float seed;
};

StructuredBuffer<Particle> gParticles : register(t0);

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

struct VSIn
{
    float2 pos : POSITION;
    float2 uv : TEXCOORD0;
    uint iid : SV_InstanceID;
};

struct VSOut
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

VSOut main(VSIn vin)
{
    VSOut o;

    Particle p = gParticles[vin.iid];

    o.uv = vin.uv;
    o.life = p.life;
    o.seed = p.seed;

    // dead ‚Í‰æ–ÊŠO‚Ö
    if (p.life <= 0.0f)
    {
        o.svpos = float4(-1e8, -1e8, 1, 1);
        o.wpos = 0;
        return o;
    }

    
    float3 vApp = p.vel - gCameraVel;
    float speed = max(length(vApp), 0.001);
    float3 dir = vApp / speed;

    float3 toCam = normalize(gCameraPos - p.pos);

   
    float3 side = normalize(cross(dir, toCam));
    float3 up = normalize(cross(side, dir));

    
    float wJit = lerp(0.70, 1.25, Hash11(p.seed + 1.7));
    float lJit = lerp(0.75, 1.35, Hash11(p.seed + 8.3));

    // • / ’·‚³
    float width = 0.015 * wJit;
    float length = lerp(0.20, 0.70, saturate(speed / 35.0)) * lJit;

    
    float x = vin.pos.x; 
    float y = vin.pos.y; 

    float3 wpos =
        p.pos
        + side * (x * width)
        + (-dir) * (y * length); 

    o.wpos = wpos;
    o.svpos = mul(float4(wpos, 1.0), gViewProj);
    return o;
}
