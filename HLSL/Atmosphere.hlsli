cbuffer ATMOSPHERE_CONSTANT_BUFFER : register(b3)
{
    float3 sunDirection;
    float sunIntensity;
    float3 cameraPosition;
    float nightIntensity;
    float planetRadius; 
    float atmosphereRadius;
    float rayleighScaleHeight;
    float mieScaleHeight;
    float3 rayleighScatteringCoefficient;
    float mieScatteringCoefficient;
    float mieEccentricity;
    float3 _padding2;
};

//------------------------------------------------------------------------------
//  大気の消散（散乱 + 吸収）に関する共通定義
//  Transmittance LUT と空の散乱積分で同じモデルを使うためここに置く
//------------------------------------------------------------------------------

// エアロゾル（ミー散乱粒子）は散乱だけでなく吸収も行うため、
// 消散係数は散乱係数より少し大きくなる
static const float MIE_EXTINCTION_RATIO = 1.11f;

// オゾン層による吸収係数（US標準大気モデル, 単位: 1/km）
// 赤と緑を選択的に吸収するため、薄明時に空が青紫に残る現象を再現できる
static const float3 OZONE_ABSORPTION_COEFFICIENT = float3(0.000650f, 0.001881f, 0.000085f);
static const float OZONE_CENTER_ALTITUDE_KM = 25.0f;
static const float OZONE_LAYER_HALF_WIDTH_KM = 15.0f;

// 高度[km]におけるオゾン密度（高度25kmをピークとする三角形分布で近似）
float GetOzoneDensity(float altitudeKm)
{
    return max(0.0f, 1.0f - abs(altitudeKm - OZONE_CENTER_ALTITUDE_KM) / OZONE_LAYER_HALF_WIDTH_KM);
}

// 高度[km]における消散係数（レイリー散乱 + ミー消散 + オゾン吸収）
// レイリー／ミーの密度は散乱項の計算でも使うので同時に返す
float3 GetAtmosphereExtinction(float altitudeKm, out float outRayleighDensity, out float outMieDensity)
{
    outRayleighDensity = exp(-altitudeKm / max(rayleighScaleHeight, 1e-4f));
    outMieDensity = exp(-altitudeKm / max(mieScaleHeight, 1e-4f));

    return outRayleighDensity * rayleighScatteringCoefficient
         + outMieDensity * (mieScatteringCoefficient * MIE_EXTINCTION_RATIO)
         + GetOzoneDensity(altitudeKm) * OZONE_ABSORPTION_COEFFICIENT;
}