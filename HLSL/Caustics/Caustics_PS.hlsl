struct PSIn
{
    float4 SVPosition : SV_POSITION;
    float3 OldPos : TEXCOORD0;
    float3 NewPos : TEXCOORD1;
    float ShouldDiscard : TEXCOORD2;
};

cbuffer CausticsCB : register(b8)
{
    row_major float4x4 gInvViewProjection;
    float4 params; // x=scale, y=wobble, z=power, w=intensity
    float2 invScreenSize;
    float2 _pad;
    float time;
    float waterPlaneY;
    float2 _padTime;
    float4 lightDirection;
};

float4 main(PSIn IN) : SV_TARGET
{
    // 頂点シェーダ側で画面外や交差失敗と判定された場合はピクセルを破棄
    if (IN.ShouldDiscard < 0.0f)
    {
        discard;
    }

    float3 oldPos = IN.OldPos;
    float3 newPos = IN.NewPos;

    // 偏微分(ddx/ddy)を利用して、屈折前のメッシュ面積を近似
    float dx_old = length(ddx(oldPos));
    float dy_old = length(ddy(oldPos));
    // 同様に屈折後の面積を近似
    float dx_new = length(ddx(newPos));
    float dy_new = length(ddy(newPos));

    float oldArea = dx_old * dy_old;
    float newArea = dx_new * dy_new;

    // 面積が極端に小さい、あるいは不正な値(NaN/Inf)になった場合はアーティファクト防止のため破棄
    if (newArea < 1e-5f || isnan(newArea) || isinf(newArea))
    {
        discard;
    }

    float absorptionCoeff = 0.0005f;
    float dist = length(newPos - oldPos);
    dist = min(dist, 3000.0f); // 距離による減衰のクランプ

    // 面積比から集光の強さを計算し、水深による吸収減衰を掛ける。
    float focusRatio = (oldArea / newArea) * exp(-absorptionCoeff * dist);
    float broadLight = smoothstep(0.90f, 1.20f, focusRatio);
    float brightRidge = smoothstep(1.12f, 1.85f, focusRatio);
    float col = broadLight * 0.24f + brightRidge * 0.76f;

    // 強度を有界に保ち、白い塗りの飛び出しを防ぐ。
    col = pow(saturate(col), max(params.z, 0.35f));
    col *= params.w;
    col = min(col, 1.5f);

    float fadeStart = 30000.0f; // フェードアウトが始まる距離
    float fadeEnd = 60000.0f; // 完全にコースティクスが消える距離
    
    // IN.SVPosition.w はカメラからの深度。これを使って 0.0 ~ 1.0 の減衰率を作る
    float fadeFactor = saturate((fadeEnd - IN.SVPosition.w) / (fadeEnd - fadeStart));
    
    
    fadeFactor = smoothstep(0.0f, 1.0f, fadeFactor);
    
    // 最終的なカラーに減衰率を乗算
    col *= fadeFactor;
    
    return float4(col, 0.0f, 0.0f, 1.0f);
}
