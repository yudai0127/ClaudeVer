#include "Water.hlsli"
#include "../Atmosphere.hlsli" 
#include "../Sampler.hlsli"

Texture2D NormalMap0 : register(t0);
Texture2D NormalMap1 : register(t1);
Texture2D NormalMap2 : register(t2);

// 背景
Texture2D SceneColor : register(t3);

// 環境反射
TextureCube SkyCube : register(t4);

// SSR反射
Texture2D SSRColor : register(t5);

// 波紋の高さマップ
Texture2D RippleDisplacementMap : register(t6);

Texture2D SceneDepth : register(t7);
Texture2D SceneNormal : register(t8);
Texture2D CausticsMap : register(t9);



// Fresnel
inline float3 SchlickFresnel(float3 F0, float cosTheta)
{
   
    return F0 + (1.0f - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 RunRefraction(float2 seed, Ray refractedRay, out float3 worldPosBehind)
{
    float depth = SceneDepth.Sample(sampler_states[ClampPoint], seed).r;
    float ndcDepth = depth;

    worldPosBehind = GetWorldPosFromDepth(seed, ndcDepth);

    float3 normal = normalize(SceneNormal.Sample(sampler_states[ClampLinear], seed).xyz);

    Plane plane;
    plane.origin = worldPosBehind;
    plane.normal = normal;

    float t = PlaneRayIntersection(plane, refractedRay);
    if (t >= 0.0f)
    {
        return refractedRay.start + refractedRay.dir * t;
    }
    return float3(-1.0f, -1.0f, -1.0f);
}

float4 main(PSIn IN) : SV_TARGET
{
   
    //ノーマルをスクロール合成
    float2 uv = IN.UV;

   
    float2 uv0 = uv * normal0.z + normal0.xy * gTime;
    float2 uv1 = uv * normal1.z + normal1.xy * gTime;
    float2 uv2 = uv * normal2.z + normal2.xy * gTime;

    
    float3 n0 = NormalMap0.Sample(sampler_states[WrapLinear], uv0).xyz * 2.0f - 1.0f;
    float3 n1 = NormalMap1.Sample(sampler_states[WrapLinear], uv1).xyz * 2.0f - 1.0f;
    float3 n2 = NormalMap2.Sample(sampler_states[WrapLinear], uv2).xyz * 2.0f - 1.0f;

    float3 nTS_Gerstner = normalize(n0 * normal0.w + n1 * normal1.w + n2 * normal2.w);

    
    //波紋高さマップから “波紋法線” を差分で作る
    // du/dv はUV上の微小オフセット
    float2 du = float2(rippleParams.y, 0.0f);
    float2 dv = float2(0.0f, rippleParams.z);

    // 高さをサンプル（中心/右/上）
    float h_c = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv, 0).x;
    float h_u = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv + du, 0).x;
    float h_v = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv + dv, 0).x;

    
    float3 rippleN_TS = normalize(float3((h_c - h_u) * rippleParams.w,
                                         (h_c - h_v) * rippleParams.w,
                                         1.0f));

   
    float3 nTS = normalize(nTS_Gerstner + rippleN_TS);

    
    //タンジェント空間法線 → ワールド法線へ（TBN変換）
    float3 T = IN.WorldTangent;
    float3 B = IN.WorldBitangent;

  
    float3 N_world_fromMap = normalize(nTS.x * T + nTS.y * B + nTS.z * IN.WorldNorm);

  
    float3 N = normalize(N_world_fromMap);

    // 視線V / 反射方向R を作る
    
    // 視線：水面点 → カメラ
    float3 V = normalize(gCameraPos - IN.WorldPos);

    // reflect(I,N) なので I = -V（カメラから水面へ向かう向き）を入れる
    float3 R = reflect(-V, N);

    
    
    R.y = max(R.y, 0.05f);
    R = normalize(R);
    
    //画面座標 screenUV を作る
    // クリップ→NDC（-1..1）
    float2 ndc = IN.ClipPos.xy / IN.ClipPos.w;

    // NDC→UV（0..1）へ（Y反転込み）
    float2 screenUV = ndc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    float waterDepthNdc = IN.ClipPos.z / IN.ClipPos.w;
    float sceneDepthHere = SceneDepth.Sample(sampler_states[ClampPoint], screenUV).r;

    // ここで “手前に別ジオメトリがある” なら水は描かない
    if (sceneDepthHere < waterDepthNdc - 1e-4f)
        discard;
    
    Ray refractedRay;
    refractedRay.start = IN.WorldPos;
    refractedRay.dir = refract(-V, N, 1.0f / WaterIOR);

    float2 refractionUV = screenUV;
    float2 seed = screenUV;
    float3 worldPosBehind = IN.WorldPos;

    float3 worldRefractionPos = float3(-1.0f, -1.0f, -1.0f);

    [unroll]
    for (uint i = 0; i < RefractionIterationCount; ++i)
    {
        worldRefractionPos = RunRefraction(seed, refractedRay, worldPosBehind);
        if (all(worldRefractionPos < -0.5f))
        {
            break;
        }

        float4 clip = mul(float4(worldRefractionPos, 1.0f), gViewProjection);
        float2 refractionNdc = clip.xy / clip.w;
        refractionUV = refractionNdc * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        seed = refractionUV;
    }

    float2 distortionBase = N_world_fromMap.xz * misc.y;
    float2 fallbackRefractionUV = screenUV + distortionBase;

    float fade = 0.0f;
    float beginFade = 0.1f;
    float distFromEdge = DistanceFromEdge(refractionUV);

    if (!IsPointValid(refractionUV))
    {
        fade = 1.0f;
    }
    else if (distFromEdge < beginFade)
    {
        fade = (beginFade - distFromEdge) / beginFade;
    }

    float resultDepth = SceneDepth.Sample(sampler_states[ClampPoint], refractionUV).r;
    float3 resultWorldPos = GetWorldPosFromDepth(refractionUV, resultDepth);

    if (resultDepth < waterDepthNdc - 1e-4f)
    {
        fade = 1.0f;
    }
    
    float2 closestPoint = FindClosestPointOnRay(resultWorldPos, refractedRay) * gScreenSize;
    float2 curPoint = refractionUV * gScreenSize;

    if (distance(curPoint, closestPoint) > RefractionErrorPixels)
    {
        fade = 1.0f;
    }

    if (SceneDepth.Sample(sampler_states[ClampPoint], fallbackRefractionUV).r < waterDepthNdc || !IsPointValid(fallbackRefractionUV))
    {
        fallbackRefractionUV = screenUV;
    }

    float2 refrUV = saturate(lerp(refractionUV, fallbackRefractionUV, saturate(fade)));
    
    
    float finalDepth = SceneDepth.Sample(sampler_states[ClampPoint], refrUV).r;
    float3 finalWorldPosBehind = GetWorldPosFromDepth(refrUV, finalDepth);

    float3 refr = SceneColor.Sample(sampler_states[ClampLinear], refrUV).rgb;
    float3 causticsColor = float3(0.0f, 0.0f, 0.0f);
    
   
    if (finalWorldPosBehind.y < IN.WorldPos.y)
    {
        float causticSample = CausticsMap.Sample(sampler_states[ClampLinear], refrUV).r;
        causticSample = max(causticSample, 0.0f);
    
        // 水深の計算にも finalWorldPosBehind を使う
        float waterDepth = IN.WorldPos.y - finalWorldPosBehind.y;
        float causticFade = saturate(exp(-waterDepth * 0.0005f));
    
        float3 causticLightColor = float3(1.5f, 1.8f, 2.0f);
        causticsColor = causticLightColor * causticSample * alphaParam.y * causticFade;
    }

    // 屈折カラーに加算する
    refr += causticsColor;
   
   //反射の計算
    float2 ssrUV = saturate(screenUV + distortionBase);
    float4 ssrSample = SSRColor.Sample(sampler_states[ClampLinear], ssrUV);
    float2 edgeFactor = saturate(abs(ndc) * 1.1f - 0.1f);
    float screenFade = saturate(1.0f - max(edgeFactor.x, edgeFactor.y));
    float ssrWeight = saturate(ssrSample.a) * screenFade;
    
    
   
  
    
    float3 envRefl = SkyCube.Sample(sampler_states[WrapLinear], R).rgb;
    
    float3 refl = lerp(envRefl, ssrSample.rgb, ssrWeight);
    // フレネルの計算
    float cosTheta = saturate(dot(N, V));
    float3 F0 = float3(misc.x, misc.x, misc.x);
    float3 F = SchlickFresnel(F0, cosTheta);
    float fresPow = max(iblParams.z, 1.0f);
    float f = saturate(pow(F.x, fresPow));

    
    float reflWeight = saturate(max(f, iblParams.x));
   
    float thickness = shadingParams.x / max(cosTheta, 0.15f);
    float att = exp(-waterTint.a * thickness);

   
    float3 refrTinted = lerp(waterTint.rgb, refr, att);

    

   
    
    float3 L_sun = normalize(-sunDirection);

   
    float3 H_sun = normalize(L_sun + V);

    
    float specSun = pow(saturate(dot(N, H_sun)), 512.0f) * sunIntensity;


    float sunLuma = dot(float3(1.0f, 1.0f, 1.0f),
                        float3(0.2126f, 0.7152f, 0.0722f));

    
    float3 spec = specSun * float3(1.0f, 1.0f, 1.0f) * saturate(sunLuma + 0.0001f);

   
   
   // 最終合成
    float3 baseColor = (misc.w > 0.5f) ? float3(0.02f, 0.06f, 0.12f) : refrTinted;
   
    
    
    

    float3 subsurface = waterTint.rgb * alphaParam.x * (1.0f - att);
    float3 color = lerp(baseColor, refl * iblParams.y, reflWeight) + spec + subsurface;
 
    return float4(color, 1.0f);
}


