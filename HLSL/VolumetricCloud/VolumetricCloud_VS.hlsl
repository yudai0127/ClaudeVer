#include "VolumetricCloud.hlsli"

VS_OUT main(in uint vertexid : SV_VERTEXID)
{
    VS_OUT vout;
    // 頂点の位置を定義
    const float2 position[4] = { { -1, +1 }, { +1, +1 }, { -1, -1 }, { +1, -1 } };
    // テクスチャ座標を定義
    const float2 texcoords[4] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
    // 頂点IDに基づいて位置とテクスチャ座標を設定
    vout.position = float4(position[vertexid], 1.0, 1.0);
    vout.texcoord = texcoords[vertexid];
    return vout;
}