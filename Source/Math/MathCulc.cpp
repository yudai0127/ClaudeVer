#include <stdlib.h>
#include "MathCulc.h"

float MathCulc::Lerp(float a, float b, float t)
{
    return a * (1.0f - t) + (b * t);
}

DirectX::XMFLOAT3 MathCulc::Lerp(DirectX::XMFLOAT3 pos, DirectX::XMFLOAT3 target, float t)
{
    DirectX::XMFLOAT3 result;
    DirectX::XMStoreFloat3(&result, DirectX::XMVectorLerp(DirectX::XMLoadFloat3(&pos), DirectX::XMLoadFloat3(&target), t));
    return result;
}

float MathCulc::RandomRange(float min, float max)
{
    int randint = rand();
    float randfloat = (float)randint / RAND_MAX;
    return min + randfloat * (max - min);
}