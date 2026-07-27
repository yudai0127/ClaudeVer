#include "Skymap.hlsli" 
#include "../Sampler.hlsli"
static const float PI = 3.14159265;

// 積分サンプル数
static const int NUM_SAMPLES = 48;

RWTexture2DArray<float4> g_SkyCubemap : register(u0);


Texture2D<float4> g_TransmittanceLUT : register(t0);


[numthreads(8, 8, 1)]
void main(uint3 dispatchID : SV_DispatchThreadID)
{
    //視線方向の計算
    uint width, height, numFaces;
    g_SkyCubemap.GetDimensions(width, height, numFaces);
    if (dispatchID.x >= width || dispatchID.y >= height || dispatchID.z >= numFaces)
        return;
    float2 uv = (float2(dispatchID.xy) + 0.5f) / float2(width, height);
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    
    float3 worldPosKm;
    switch (dispatchID.z)
    {
        case 0:
            worldPosKm = normalize(float3(1.0, ndc.y, -ndc.x));
            break;
        case 1:
            worldPosKm = normalize(float3(-1.0, ndc.y, ndc.x));
            break;
        case 2:
            worldPosKm = normalize(float3(ndc.x, 1.0, -ndc.y));
            break;
        case 3:
            worldPosKm = normalize(float3(ndc.x, -1.0, ndc.y));
            break;
        case 4:
            worldPosKm = normalize(float3(ndc.x, ndc.y, 1.0));
            break;
        case 5:
            worldPosKm = normalize(float3(-ndc.x, ndc.y, -1.0));
            break;
        default:
            worldPosKm = normalize(float3(1.0, 0.0, 0.0));
            break;
    }

    float3 oc = cameraPosition;
    float b = dot(oc, worldPosKm);
    float posSq = dot(oc, oc);

    // 大気圏・地面との交差判定
    float c_atm = posSq - (atmosphereRadius * atmosphereRadius);
    float det_atm = b * b - c_atm;
    bool cameraInAtmosphere = (c_atm < 0.0);
    
    if (det_atm < 0.0)
    {
        g_SkyCubemap[dispatchID] = float4(0, 0, 0, 1.0);
        return;
    }
    float sqrt_det_atm = sqrt(det_atm);
    float2 intersection = float2(-b - sqrt_det_atm, -b + sqrt_det_atm);
    
    float c_grd = posSq - (planetRadius * planetRadius);
    float det_grd = b * b - c_grd;
    float tGround = -1.0;
    bool rayHitsGround = false;
    
    if (det_grd >= 0.0)
    {
        float sqrt_det_grd = sqrt(det_grd);
        float t0 = -b - sqrt_det_grd;
        float t1 = -b + sqrt_det_grd;
        if (t0 > 0.0)
            tGround = t0;
        else if (t1 > 0.0)
            tGround = t1;
        rayHitsGround = (tGround > 0.0);
    }

    // 積分区間の決定
    float tStart, tEnd;
    if (cameraInAtmosphere)
    {
        tStart = 0.0;
        tEnd = rayHitsGround ? tGround : intersection.y;
    }
    else
    {
        tStart = intersection.x;
        tEnd = rayHitsGround ? tGround : intersection.y;
    }

    if (tEnd <= 0.0)
    {
        g_SkyCubemap[dispatchID] = float4(0, 0, 0, 1.0);
        return;
    }

   
    //散乱計算
    float rayLength = tEnd - tStart;
    float3 accumulatedRayleigh = 0;
    float3 accumulatedMie = 0;
    float3 viewOpticalDepth = 0;
    float prevT = tStart;

    for (int i = 0; i < NUM_SAMPLES; ++i)
    {
        // 非線形サンプリング
        float t_norm = (float(i) + 1.0) / (float) NUM_SAMPLES;
        float t_non_linear = pow(t_norm, 2.0);
        float currentT = tStart + rayLength * t_non_linear;
        float variableStepSize = currentT - prevT;
        float sampleT = prevT + variableStepSize * 0.5;
        float3 currentPos = cameraPosition + worldPosKm * sampleT;
        float currentPosLen = length(currentPos);
        float height = currentPosLen - planetRadius;

        if (height <= 0.0)
        {
            prevT = currentT;
            continue;
        }
            
        // 指数分布に基づいた大気密度の計算
        float densityRayleigh = exp(-height / rayleighScaleHeight);
        float densityMie = exp(-height / mieScaleHeight);

        // 太陽光の透過率の取得
        float3 posNorm = normalize(currentPos);
        float mu_for_lut = dot(posNorm, sunDirection);
        float u_lut = saturate((mu_for_lut + 1.0) * 0.5);
        float r_for_lut = currentPosLen;
        float v_lut = saturate((r_for_lut - planetRadius) / (atmosphereRadius - planetRadius));
        float3 transmittanceLight = g_TransmittanceLUT.SampleLevel(sampler_states[ClampLinear], float2(u_lut, v_lut), 0).rgb;

        // 地平線以下に太陽がある場合の遮蔽判定
        float3 oc_light = currentPos;
        float b_light = dot(oc_light, sunDirection);
        float posSq_light = currentPosLen * currentPosLen;
        float c_grd_light = posSq_light - (planetRadius * planetRadius);
        float det_grd_light = b_light * b_light - c_grd_light;
       bool lightOccludedByPlanet = false;
        if (det_grd_light >= 0.0)
        {
            float tHitLight = -b_light - sqrt(det_grd_light);
            lightOccludedByPlanet = (tHitLight > 0.0);
        }

        float3 transmittance = lightOccludedByPlanet ? float3(0, 0, 0) : transmittanceLight;
        
       
        viewOpticalDepth += densityRayleigh * variableStepSize * rayleighScatteringCoefficient;
        viewOpticalDepth += densityMie * variableStepSize * mieScatteringCoefficient;

        // 位相関数（Phase Function）：光がどの方向に散乱するかを制御
        float mu = dot(worldPosKm, sunDirection);
        // レイリー散乱：ほぼ全方向に均等に散乱
        float rayleighPhase = (3.0 / (16.0 * PI)) * (1.0 + mu * mu);
        // ミー散乱：前方への散乱が強い
        float miePhase = (1.0 / (4.0 * PI)) * (1.0 - mieEccentricity * mieEccentricity)
                       / pow(1.0 + mieEccentricity * mieEccentricity - 2.0 * mieEccentricity * mu, 1.5);
        
       
        float3 attenuation = exp(-viewOpticalDepth);

        accumulatedRayleigh += densityRayleigh * variableStepSize * rayleighPhase * transmittance * attenuation;
        accumulatedMie += densityMie * variableStepSize * miePhase * transmittance * attenuation;
        
        prevT = currentT;
    }
    
    

    // 最終色の合成
    float3 finalColor = sunIntensity * (accumulatedRayleigh * rayleighScatteringCoefficient
                                      + accumulatedMie * mieScatteringCoefficient);

    
   
    
    
    g_SkyCubemap[dispatchID] = float4(finalColor, 1.0);
}