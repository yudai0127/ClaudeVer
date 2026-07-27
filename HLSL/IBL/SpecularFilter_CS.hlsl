TextureCube g_SourceSky : register(t0);
SamplerState g_SamplerLinear : register(s0);

cbuffer SpecularConstants : register(b0)
{
    float roughness;
    float3 padding;
    uint numSamples;
    float3 padding2;
};


RWTexture2DArray<float4> g_FilteredMap : register(u0);

static const float PI = 3.14159265;

static const uint NUM_SAMPLES = 1024;

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


float3 ImportanceSampleGGX(float2 r, float roughness, float3 N)
{
    float a = roughness * roughness;
    float a2 = a * a;
    
    // 接空間（TBN）を作成
    float3 up = abs(N.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);
    
    // GGX分布のサンプリング
    float phi = 2.0 * PI * r.x;
    float cosTheta = sqrt((1.0 - r.y) / (1.0 + (a2 - 1.0) * r.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    
    // 接空間でのハーフベクトルH
    float3 H_tangent = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    
    // ワールド空間に変換
    return T * H_tangent.x + B * H_tangent.y + N * H_tangent.z;
}

// (u, v, faceIndex) から 3D方向ベクトルを計算
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
            break;
        case 1:
            dir = normalize(float3(-1.0, ndc.y, ndc.x));
            break; 
        case 2:
            dir = normalize(float3(ndc.x, 1.0, ndc.y));
            break; 
        case 3:
            dir = normalize(float3(ndc.x, -1.0, -ndc.y));
            break; 
        case 4:
            dir = normalize(float3(ndc.x, ndc.y, 1.0));
            break; 
        case 5:
            dir = normalize(float3(-ndc.x, ndc.y, -1.0));
            break; 
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
    g_FilteredMap.GetDimensions(width, height, depth);
    
   
    float3 N = UVToDirection(dispatchID, uint2(width, height));
    float3 V = N; // 視線ベクトル
    
    float3 prefilteredColor = 0.0f;
    float totalWeight = 0.0f;
    
    for (uint i = 0; i < numSamples; ++i)
    {
        float2 r = Hammersley(i, numSamples);
        
        // ラフネスに基づいてハーフベクトルHをサンプリング
        float3 H = ImportanceSampleGGX(r, roughness, N);
        
        // VとHからライトベクトルLを計算 
        float3 L = 2.0 * dot(V, H) * H - V;
        
        float NdotL = saturate(dot(N, L));
        if (NdotL > 0.0)
        {
           prefilteredColor += g_SourceSky.SampleLevel(g_SamplerLinear, L, 0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    
    // 正規化
    prefilteredColor = (totalWeight > 0.0) ? (prefilteredColor / totalWeight) : 0.0f;
    
    g_FilteredMap[dispatchID] = float4(prefilteredColor, 1.0);
}