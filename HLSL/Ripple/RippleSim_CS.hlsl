// 入力
Texture2D<float2> gIn : register(t0);
// 出力
RWTexture2D<float2> gOut : register(u0);

cbuffer SimCB : register(b0)
{
    float2 texel; 
    float dt; 
    float c; // 波の速度
    float damping; // 減衰
    float2 _pad;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // サイズ取得
    uint w, h;
    gOut.GetDimensions(w, h);

    // スレッド範囲外は無視
    if (id.x >= w || id.y >= h)
        return;

    
    float2 uv = (float2(id.xy) + 0.5f) / float2(w, h);

    // 中心セルの (h, v)
    float2 c0 = gIn.Load(int3(id.xy, 0));
    float hC = c0.x; 
    float vC = c0.y; 

    
    uint2 l = uint2(max(int(id.x) - 1, 0), id.y);
    uint2 r = uint2(min(id.x + 1, w - 1), id.y);
    uint2 t = uint2(id.x, max(int(id.y) - 1, 0));
    uint2 b = uint2(id.x, min(id.y + 1, h - 1));

   
    float hL = gIn.Load(int3(l, 0)).x;
    float hR = gIn.Load(int3(r, 0)).x;
    float hT = gIn.Load(int3(t, 0)).x;
    float hB = gIn.Load(int3(b, 0)).x;

   
    float lap = (hL + hR + hT + hB - 4.0f * hC);

    // 速度更新：
    
    float vN = (vC + (c * c) * dt * lap) * (1.0f - damping * dt);

    // 高さ更新：
    
    float hN = hC + vN * dt;

    // 次状態を保存
    gOut[id.xy] = float2(hN, vN);
}
