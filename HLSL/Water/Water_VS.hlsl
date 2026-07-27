#include "Water.hlsli"
#include "../Sampler.hlsli"
Texture2D RippleDisplacementMap : register(t6);

VSOutput main(VSInput input)
{
    VSOutput o;

    // ローカル座標からワールド座標へ変換
    float4 wp = mul(float4(input.Position, 1.0f), gWorld);
    float3 worldPos0 = wp.xyz;

    float3 worldPos, worldN, worldT, worldB;
    // ゲルストナー波による変位と法線・接空間ベクトルを適用
    ApplyGerstner(worldPos0, worldPos, worldN, worldT, worldB);

    // 波紋マップから高さをサンプリングし、頂点のY座標に加算
    float rippleHeight = RippleDisplacementMap.SampleLevel(sampler_states[WrapLinear], input.TexCoord, 0).x;
    worldPos.y += rippleHeight * rippleParams.x;

    // プロジェクション空間へ変換
    float4 clip = mul(float4(worldPos, 1.0f), gViewProjection);
    
    o.SVPosition = clip;
    o.ClipPos = clip;
    o.WorldPos = worldPos;
    
    // ピクセルシェーダのTBN計算用に接空間ベクトルを渡す
    o.WorldNorm = worldN;
    o.WorldTangent = worldT;
    o.WorldBitangent = worldB;
    o.UV = input.TexCoord;

    return o;
}