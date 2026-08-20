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



// 遠景でノーマルの細部を落としきる距離（shadingParams.z が未設定のときのフォールバック）
static const float WaterDetailFadeDistance = 20000.0f;

// 地平線付近の反射ベクトルの下限とブレンド幅
static const float WaterHorizonReflectionFloor = 0.06f;
static const float WaterHorizonReflectionBlend = 0.18f;

// 太陽のスペキュラローブ。近景は鋭く、遠景は広げてちらつきを防ぐ
static const float WaterSpecularShininessNear = 1024.0f;
static const float WaterSpecularShininessFar = 48.0f;
static const float WaterSpecularClamp = 64.0f;

// 水中光路長の上限（水底が取れないときに exp() が完全に潰れるのを防ぐ）
static const float WaterMaxPathLength = 10000.0f;

// 波長ごとの吸収の強さ。水は赤をもっとも強く吸収する
static const float WaterAbsorptionMaxRatio = 3.0f;

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

    // 遠景では1ピクセルに多数の波が収まり、ノーマルマップのタイリングが
    // 縞状のちらつき（スペキュラエイリアシング）として見えてしまう。
    // 距離に応じて高周波レイヤーの寄与を落とし、代わりに粗さを上げる
    float viewDistance = length(gCameraPos - IN.WorldPos);
    float detailFadeDistance = (shadingParams.z > 1.0f) ? shadingParams.z : WaterDetailFadeDistance;
    float distFade = saturate(viewDistance / detailFadeDistance);

    // 異方性フィルタリングで、浅い角度から見たときのストリーク状のにじみを軽減
    float3 n0 = NormalMap0.Sample(sampler_states[WrapAnisotropic], uv0).xyz * 2.0f - 1.0f;
    float3 n1 = NormalMap1.Sample(sampler_states[WrapAnisotropic], uv1).xyz * 2.0f - 1.0f;
    float3 n2 = NormalMap2.Sample(sampler_states[WrapAnisotropic], uv2).xyz * 2.0f - 1.0f;

    // 周波数が高いレイヤーほど強く減衰させる
    float weight0 = normal0.w;
    float weight1 = normal1.w * (1.0f - distFade * 0.80f);
    float weight2 = normal2.w * (1.0f - distFade * 0.95f);

    float3 nTS_Gerstner = normalize(n0 * weight0 + n1 * weight1 + n2 * weight2);

    
    //波紋高さマップから “波紋法線” を差分で作る
    // du/dv はUV上の微小オフセット
    float2 du = float2(rippleParams.y, 0.0f);
    float2 dv = float2(0.0f, rippleParams.z);

    // 高さをサンプル（中心/右/上）
    float h_c = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv, 0).x;
    float h_u = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv + du, 0).x;
    float h_v = RippleDisplacementMap.SampleLevel(sampler_states[WrapAnisotropic], uv + dv, 0).x;
    float rippleSlope = length(float2(h_u - h_c, h_v - h_c)) * rippleParams.w;

    
    // 波紋も遠景では解像できないので同様に減衰させる
    float rippleNormalStrength = rippleParams.w * (1.0f - distFade * 0.90f);
    float3 rippleN_TS = normalize(float3((h_c - h_u) * rippleNormalStrength,
                                         (h_c - h_v) * rippleNormalStrength,
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

    
    
    // 反射ベクトルが地平線より下を向くとキューブマップの暗い下半球を拾ってしまう。
    // ハードクランプは境目に不連続な線を作るため、なめらかに水平方向へ寄せる
    R.y = lerp(R.y, WaterHorizonReflectionFloor,
               saturate(1.0f - R.y / WaterHorizonReflectionBlend));
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
    float hasUnderwaterSurface = (finalDepth < 0.9999f && finalWorldPosBehind.y < IN.WorldPos.y) ? 1.0f : 0.0f;

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
    // SSR のレイ方向には GBuffer prepass の波法線が既に使われている。
    // ここでも屈折用 distortion を加えると二重に歪み、船の反射が本体から
    // ずれて薄く見えるため、同じ画面座標から結果を取得する。
    float2 ssrUV = screenUV;
    float4 ssrSample = SSRColor.Sample(sampler_states[ClampLinear], ssrUV);
    float2 edgeFactor = saturate(abs(ndc) * 1.1f - 0.1f);
    float screenFade = saturate(1.0f - max(edgeFactor.x, edgeFactor.y));
    float ssrPresence = max(iblParams.w, 0.0f);
    float ssrConfidence = saturate(ssrSample.a * ssrPresence);
    // SSR のアルファは「反射の強さ」ではなく、画面内ヒットの信頼度として使う。
    float ssrWeight = smoothstep(0.02f, 0.70f, ssrConfidence) * screenFade;
    
    
   
  
    
    float3 envRefl = SkyCube.Sample(sampler_states[WrapLinear], R).rgb;

    // SSR は画面内の船・地形、キューブマップは画面外と空を補う。
    // ここでコントラストや最低反射量を足すと、水面に白い板が貼られたように
    // 見えるため、取得した放射輝度をそのままブレンドする。
    float3 refl = lerp(envRefl, max(ssrSample.rgb, (float3) 0.0f), ssrWeight);
    // フレネルの計算
    float cosTheta = saturate(dot(N, V));
    float3 F0 = float3(misc.x, misc.x, misc.x);
    float3 F = SchlickFresnel(F0, cosTheta);
    // 水の正面反射率は約2%。ただしHDR圧縮後の明るい水面では、そのままだと
    // 暗い船の反射が知覚できない。固定の最低反射は使わず、Fresnel曲線を
    // 1-(1-F)^k で滑らかに広げる。F=0/1の端点と角度依存性は維持される。
    float reflectionReadability = max(iblParams.x, 1.0f);
    float finalReflectionWeight = saturate(1.0f - pow(saturate(1.0f - F.x), reflectionReadability));

    //--------------------------------------------------------------------------
    // 水中での吸収（Beer-Lambert）
    // 以前は視線角だけから求めた一定の厚みを使っていたため、浅瀬でも外洋でも
    // 同じ色になっていた。実際の水底までの深さから光路長を求めることで、
    // 浅い所は透明～ターコイズ、深い所は濃い青へと自然に変化する
    //--------------------------------------------------------------------------
    float bottomDepth = max(IN.WorldPos.y - finalWorldPosBehind.y, 0.0f);
    float depthScale = (shadingParams.y > 0.0f) ? shadingParams.y : 1.0f;
    // 視線が浅いほど水中を長く通る
    float pathLength = min((bottomDepth * depthScale + shadingParams.x) / max(cosTheta, 0.15f),
                           WaterMaxPathLength);

    // ティント色を波長ごとの吸収係数へ写像する。
    // 暗い成分（＝水が吸収する色）ほど消散係数が大きい
    float3 tintNormalized = saturate(waterTint.rgb / max(max(waterTint.r, waterTint.g),
                                                         max(waterTint.b, 1e-3f)));
    float3 absorptionRatio = lerp(float3(WaterAbsorptionMaxRatio, WaterAbsorptionMaxRatio, WaterAbsorptionMaxRatio),
                                  float3(1.0f, 1.0f, 1.0f),
                                  tintNormalized);
    // Turbidity raises extinction and changes the in-scattered color.  This
    // gives the UI a physically understandable clear-to-murky control instead
    // of merely painting an opaque color over the water.
    float turbidity = saturate(alphaParam.z);
    float3 extinction = waterTint.a * absorptionRatio * lerp(1.0f, 7.0f, turbidity);
    float3 att3 = exp(-extinction * pathLength);

    // 透過してきた背景色を減衰させ、失われた分を水自身の散乱色で補う
    float3 sedimentTint = float3(0.18f, 0.14f, 0.075f);
    float3 scatteringColor = lerp(waterTint.rgb, sedimentTint, turbidity * 0.72f);
    float3 refrTinted = refr * att3 + scatteringColor * (1.0f - att3);

    //--------------------------------------------------------------------------
    // 太陽の鏡面反射
    // sunDirection は「太陽へ向かうベクトル」なので符号を反転してはいけない
    //--------------------------------------------------------------------------
    float3 L_sun = normalize(sunDirection);
    float3 H_sun = normalize(L_sun + V);

    float NdotL_sun = saturate(dot(N, L_sun));
    float NdotH_sun = saturate(dot(N, H_sun));

    // 遠景ほど1ピクセルに多数のマイクロ波面が入るのでローブを広げる
    float shininess = lerp(WaterSpecularShininessNear, WaterSpecularShininessFar, distFade);
    float specNormalization = (shininess + 8.0f) / (8.0f * PI);
    float specTerm = min(specNormalization * pow(NdotH_sun, shininess) * NdotL_sun,
                         WaterSpecularClamp);

    // 太陽が地平線より下にあるときはハイライトを消す
    float sunVisibility = saturate((sunDirection.y + 0.02f) * 8.0f);
    // 低い太陽ほど暖色に寄せる
    float3 sunColor = lerp(float3(1.0f, 0.55f, 0.25f), float3(1.0f, 0.98f, 0.92f),
                           saturate(sunDirection.y * 4.0f));
    // misc.z（GUIのSpecular）は今まで未使用だったので強度として使う
    float specIntensity = (misc.z > 0.0f) ? misc.z : 1.0f;

    // フレネルを掛けることで、浅い角度ほどハイライトが強くなる
    float3 spec = specTerm * sunColor * sunIntensity * sunVisibility * specIntensity * F;

   // 最終合成
    float3 baseColor = (misc.w > 0.5f) ? float3(0.02f, 0.06f, 0.12f) : refrTinted;

    // サブサーフェス散乱は吸収量に比例させる（輝度の平均で代表させる）
    float attLuma = dot(att3, float3(0.2126f, 0.7152f, 0.0722f));
    float3 subsurface = scatteringColor * alphaParam.x * (1.0f - attLuma);
    float3 color = lerp(baseColor, refl * iblParams.y, finalReflectionWeight) + spec + subsurface;

    // Depth-aware shoreline foam follows the actual terrain in the scene
    // depth buffer. Ripple/crest foam adds smaller moving highlights, making
    // impacts around rocks and hulls legible even when the base water is dark.
    float foamDepth = max(shadingParams.w, 1.0f);
    float shoreFoam = hasUnderwaterSurface * (1.0f - smoothstep(0.0f, foamDepth, bottomDepth));
    float foamNoise = saturate(0.52f + n0.x * 0.35f + n1.y * 0.30f);
    shoreFoam *= smoothstep(0.28f, 0.72f, foamNoise);

    // 細かいノーマルマップで泡を判定すると水面全体に白い斑点が出る。
    // 実際に形状を変位させた大きな波の傾斜だけから波頭を判定する。
    float macroSlope = 1.0f - saturate(normalize(IN.WorldNorm).y);
    float crestFoam = smoothstep(0.08f, 0.22f, macroSlope) * foamNoise;
    float impactFoam = saturate(rippleSlope * 0.075f - 0.04f);
    float foamAmount = saturate((shoreFoam + crestFoam * 0.20f + impactFoam * 0.55f)
                                * alphaParam.w * (1.0f - distFade * 0.70f));

    float sunsetAmount = (1.0f - smoothstep(0.05f, 0.35f, abs(sunDirection.y)))
                       * sunVisibility;
    float3 foamColor = lerp(float3(0.88f, 0.96f, 1.0f),
                            float3(1.0f, 0.68f, 0.42f), sunsetAmount * 0.35f);
    color = lerp(color, foamColor, foamAmount);
 
    return float4(color, 1.0f);
}


