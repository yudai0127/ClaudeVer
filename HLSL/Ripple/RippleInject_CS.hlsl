
RWTexture2D<float2> gTex : register(u0);

cbuffer InjectCB : register(b0)
{
    uint2 center; // 注入中心
    float velocity; // 速度に加算する値
    float radius; // 半径
    float falloff; // 減衰カーブ
}

[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // テクスチャサイズ
    uint w, h;
    gTex.GetDimensions(w, h);

    int rad = (int) max(radius, 0.0f);

    // 半径0
    if (rad == 0)
    {
        
        uint2 p = uint2(min(max(center.x, 0u), w - 1),
                        min(max(center.y, 0u), h - 1));

        // 現在値を読み、速度(y)だけ足す
        float2 v = gTex[p];
        v.y += velocity;
        gTex[p] = v;
        return;
    }

   
    int2 c = int2(center);

    for (int dy = -rad; dy <= rad; ++dy)
    {
        for (int dx = -rad; dx <= rad; ++dx)
        {
            int2 p = c + int2(dx, dy);

            // 範囲外はスキップ
            if ((uint) p.x >= w || (uint) p.y >= h)
                continue;

            float d = length(float2(dx, dy)); // 中心から距離
            if (d > radius)
                continue;

            
            float wgt = 1.0f - pow(saturate(d / radius), falloff);

            uint2 pp = uint2(p);

            // 速度だけ足す
            float2 v = gTex[pp];
            v.y += velocity * wgt;
            gTex[pp] = v;
        }
    }
}
