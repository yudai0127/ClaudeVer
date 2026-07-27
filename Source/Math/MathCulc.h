#pragma once
#include <DirectXMath.h>

class MathCulc
{
public:
    // 線形補間（単一の値）
    static float Lerp(float a, float b, float t);

    static DirectX::XMFLOAT3 Lerp(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 target, float t);

    //　指定範囲のランダム値を計算する
    static float RandomRange(float min, float max);
};
