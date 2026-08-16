#include "../Water/Water.hlsli"
#include "../Sampler.hlsli"


Texture2D SceneDepth : register(t0);
Texture2D NormalMap0 : register(t1);
Texture2D NormalMap1 : register(t2);

struct CausticsVSOut
{
    float4 SVPosition : SV_POSITION;
    float3 OldPos : TEXCOORD0; // 屈折前の水面座標
    float3 NewPos : TEXCOORD1; // 屈折後の海底到達座標
    float ShouldDiscard : TEXCOORD2;
};

cbuffer CausticsCB : register(b8)
{
    row_major float4x4 gInvViewProjection;
    float4 params;
    float2 invScreenSize;
    float2 _pad;
    float time;
    float waterPlaneY;
    float2 _padTime;
    float4 lightDirection;
};

CausticsVSOut main(VSInput IN)
{
    CausticsVSOut OUT;
    OUT.ShouldDiscard = 1.0f; 

    // ゲルストナー波による水面の頂点変位と接空間ベクトルを取得
    float3 waterPos, waterNorm, waterTangent, waterBitangent;
    ApplyGerstner(IN.Position, waterPos, waterNorm, waterTangent, waterBitangent);
    OUT.OldPos = waterPos;

    // テクスチャUVの計算
    float causticsUvScale = max(params.x, 0.0001f);
    float causticsWobble = max(params.y, 0.0f);
    float2 uv = IN.TexCoord * causticsUvScale;

    // 専用の低速時間を使う。水面のスクロール時間を直接使うと、
    // 小さな明るい塗りが高速で移動し「魚の群れ」のように見えやすい。
    float2 uv0 = uv * normal0.z + normal0.xy * time;
    float2 uv1 = uv * normal1.z + normal1.xy * time;

    // テクスチャから法線を取得し、-1.0?1.0の範囲に展開
    // 元テクスチャにミップがないため、対角の2点を平均して広い法線を作る。
    // これにより、魚の輪郭のような細かい孤立模様を抑える。
    uint normalWidth0, normalHeight0;
    uint normalWidth1, normalHeight1;
    NormalMap0.GetDimensions(normalWidth0, normalHeight0);
    NormalMap1.GetDimensions(normalWidth1, normalHeight1);
    float2 blurOffset0 = 2.0f / float2(max(normalWidth0, 1u), max(normalHeight0, 1u));
    float2 blurOffset1 = 2.5f / float2(max(normalWidth1, 1u), max(normalHeight1, 1u));

    float3 n0 = 0.5f * (
        NormalMap0.SampleLevel(sampler_states[WrapLinear], uv0 + blurOffset0, 0).xyz +
        NormalMap0.SampleLevel(sampler_states[WrapLinear], uv0 - blurOffset0, 0).xyz);
    float3 n1 = 0.5f * (
        NormalMap1.SampleLevel(sampler_states[WrapLinear], uv1 + float2(blurOffset1.x, -blurOffset1.y), 0).xyz +
        NormalMap1.SampleLevel(sampler_states[WrapLinear], uv1 - float2(blurOffset1.x, -blurOffset1.y), 0).xyz);
    n0 = n0 * 2.0f - 1.0f;
    n1 = n1 * 2.0f - 1.0f;

    // 2枚のノーマルマップをブレンドし、タンジェント空間での合成法線を作成
    float3 texNormTS = normalize(
        n0 * normal0.w * causticsWobble +
        n1 * normal1.w * causticsWobble +
        float3(0.0f, 0.0f, 1.0f));

    // TBNマトリクスでワールド空間の法線へ変換
    float3 finalWaterNorm = normalize(
        texNormTS.x * waterTangent +
        texNormTS.y * waterBitangent +
        texNormTS.z * waterNorm
    );

    // 太陽のレイを合成した法線で屈折させる
    float3 lightDir = normalize(lightDirection.xyz);
    float3 refractedDir = refract(lightDir, finalWaterNorm, 1.0f / WaterIOR);

    // 全反射が起きた場合 refract() はゼロベクトルを返す。
    // そのまま進めるとレイマーチが原点に留まったまま無駄にループを回すので早期に破棄する
    if (dot(refractedDir, refractedDir) < 1e-6f)
    {
        OUT.ShouldDiscard = -1.0f;
        OUT.NewPos = waterPos;
        OUT.SVPosition = mul(float4(waterPos, 1.0f), gViewProjection);
        return OUT;
    }
    refractedDir = normalize(refractedDir);

    uint w, h;
    SceneDepth.GetDimensions(w, h);

    // 水面位置のクリップ座標とUVを計算
    float4 oldClip = mul(float4(waterPos, 1.0f), gViewProjection);
    float2 oldUv = oldClip.xy / oldClip.w * float2(0.5f, -0.5f) + 0.5f;

    // 画面外、もしくはカメラより後ろにある場合は破棄
    if (!IsPointValid(oldUv) || oldClip.w < 0.0f)
    {
        OUT.ShouldDiscard = -1.0f;
        OUT.NewPos = waterPos;
        OUT.SVPosition = oldClip;
        return OUT;
    }

    int2 oldTexel = int2(oldUv.x * (float) w, oldUv.y * (float) h);
    float oldDepth = SceneDepth.Load(int3(oldTexel, 0)).r;
    float oldZ = oldClip.z / oldClip.w;

    //水面より手前に別のオブジェクトがある場合は破棄
    if (oldZ > oldDepth + 0.0001f)
    {
        OUT.ShouldDiscard = -1.0f;
        OUT.NewPos = waterPos;
        OUT.SVPosition = oldClip;
        return OUT;
    }

    // レイの最大到達距離を計算
    float3 worldPosBehind = GetWorldPosFromDepth(oldUv, oldDepth);
    float maxDist = length(worldPosBehind - waterPos);
    maxDist = max(maxDist, 1.0f);

    float3 hitPos = waterPos;
    float t = 0.0f;
    // ステップ幅は maxDist/64 なので、maxDist に到達するのに必要な回数は64。
    // 上限128は無駄なので半分に減らす（この探索は水面の頂点ごとに走るため効きが大きい）
    float stepSize = max(maxDist / 64.0f, 1.0f);
    const int maxSteps = 64;
    bool hit = false;

    // レイマーチングによる海底との交点探索
    [loop]
    for (int i = 0; i < maxSteps && t < maxDist; ++i)
    {
        t += stepSize;
        float3 p = waterPos + refractedDir * t;
        float4 clip = mul(float4(p, 1.0f), gViewProjection);
        float2 uvScreen = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;

        // レイが画面外に出た場合は探索終了
        if (uvScreen.x < 0.0f || uvScreen.x > 1.0f || uvScreen.y < 0.0f || uvScreen.y > 1.0f || clip.w < 0.0f)
        {
            break;
        }

        int2 texel = int2(uvScreen.x * (float) w, uvScreen.y * (float) h);
        float depthZ = SceneDepth.Load(int3(texel, 0)).r;
        float curZ = clip.z / clip.w;

        // 深度バッファの値と交差したか判定
        if (curZ >= depthZ)
        {
            hitPos = p;
            hit = true;
            break;
        }
    }
    
    // 交差した場合は、その区間で二分探索を行って交点座標の精度を高める
    if (hit)
    {
        float t0 = t - stepSize;
        float t1 = t;
        
        [unroll]
        for (int j = 0; j < 5; ++j)
        {
            float tMid = (t0 + t1) * 0.5f;
            float3 pMid = waterPos + refractedDir * tMid;
            float4 clipMid = mul(float4(pMid, 1.0f), gViewProjection);
            float2 uvMid = clipMid.xy / clipMid.w * float2(0.5f, -0.5f) + 0.5f;
            
            int2 texelMid = int2(uvMid.x * (float) w, uvMid.y * (float) h);
            float depthMid = SceneDepth.Load(int3(texelMid, 0)).r;
            float curZMid = clipMid.z / clipMid.w;
            
            if (curZMid >= depthMid)
                t1 = tMid;
            else
                t0 = tMid;
        }
        hitPos = waterPos + refractedDir * t1;
    }
    else
    {
        // 交差しなかった場合（レイが抜けてしまった場合）は無効
        OUT.ShouldDiscard = -1.0f;
        hitPos = waterPos + refractedDir * t;
    }

    OUT.NewPos = hitPos;
    // 最終的なクリップ座標は屈折後の位置ベース
    OUT.SVPosition = mul(float4(hitPos, 1.0f), gViewProjection);
    return OUT;
}
