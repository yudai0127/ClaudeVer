TextureCube g_SourceSky : register(t0);
SamplerState g_SamplerLinear : register(s0);

RWTexture2DArray<float4> g_IrradianceMap : register(u0);

static const float PI = 3.14159265;

static const uint NUM_SAMPLES = 256;

// 2Dのハッシュ関数
float2 Hammersley(uint index, uint N)
{
    
    uint bits = index;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);

   
    return float2(float(bits) / 4294967296.0f, float(index) / float(N));
}


float3 ImportanceSampleCos(float2 r, float3 N)
{
    // Nを基準とした接空間（TBN）を作成
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    
    // コサイン分布のサンプリング
    float phi = 2.0 * PI * r.x;
    float cosTheta = sqrt(1.0 - r.y);
    float sinTheta = sqrt(r.y);
    
    // 接空間でのサンプル方向
    float3 L_tangent = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    
    // ワールド空間に変換
    return T * L_tangent.x + B * L_tangent.y + N * L_tangent.z;
}


float3 UVToDirection(uint3 dispatchID, uint2 resolution)
{
    float2 uv = (float2(dispatchID.xy) + 0.5f) / float2(resolution);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y; // Y反転

    float3 dir;
    switch (dispatchID.z)
    {
        case 0:
            dir = normalize(float3(1.0, ndc.y, -ndc.x));
            break; // +X
        case 1:
            dir = normalize(float3(-1.0, ndc.y, ndc.x));
            break; // -X
        case 2:
            dir = normalize(float3(ndc.x, 1.0, ndc.y));
            break; // +Y
        case 3:
            dir = normalize(float3(ndc.x, -1.0, -ndc.y));
            break; // -Y
        case 4:
            dir = normalize(float3(ndc.x, ndc.y, 1.0));
            break; // +Z
        case 5:
            dir = normalize(float3(-ndc.x, ndc.y, -1.0));
            break; // -Z
        default:
            dir = float3(1, 0, 0);
            break;
    }
    return dir;
}


[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    uint width, height, depth;
    g_IrradianceMap.GetDimensions(width, height, depth);
    
    // このピクセルが担当する法線ベクトルN
    float3 N = UVToDirection(dispatchID, uint2(width, height));
    
    float3 irradiance = 0.0f;
    
    for (uint i = 0; i < NUM_SAMPLES; ++i)
    {
        float2 r = Hammersley(i, NUM_SAMPLES);
        float3 L = ImportanceSampleCos(r, N); // サンプル方向
        
        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
            irradiance += g_SourceSky.SampleLevel(g_SamplerLinear, L, 0).rgb;
        }
    }
    
    
    irradiance = irradiance * PI / (float) NUM_SAMPLES;

    g_IrradianceMap[dispatchID] = float4(irradiance, 1.0);
}