#include "VolumetricCloud.hlsli"

static const float WORLD_RANGE = 20000.0f;
static const float LIGHT_DIR_Y_MIN_ABS = 0.05f;
static const int SHADOW_MARCH_STEPS = 16;
static const float SHADOW_DENSITY_MIP_LEVEL = 3.0f;
static const float SHADOW_DENSITY_SCALE = 2.0f;

static const float4 SHADOW_OUTSIDE_COLOR = float4(1.0f, 0.0f, 0.0f, 1.0f);
static const float4 SHADOW_OUTPUT_BASE = float4(0.0f, 0.0f, 0.0f, 1.0f);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 worldXZ = (pin.texcoord - 0.5f) * WORLD_RANGE + camera_position.xz;

    float earthRadiusWithCloud = cloud_altitudes_min_max.y;
    float distSq = dot(worldXZ, worldXZ);
    float radiusSq = earthRadiusWithCloud * earthRadiusWithCloud;
    if (distSq > radiusSq)
    {
        return SHADOW_OUTSIDE_COLOR;
    }

    float sphereTopY = sqrt(radiusSq - distSq);

    // 太陽方向にレイを飛ばし、角度に応じてステップ長を補正
    float3 lightDir = normalize(sunDirection.xyz);
    float dirYAbs = max(LIGHT_DIR_Y_MIN_ABS, abs(lightDir.y));
    float3 rayPos = float3(worldXZ.x, sphereTopY, worldXZ.y);
    float3 rayDir = lightDir;

    float totalDensity = 0.0f;
    float verticalThickness = (cloud_altitudes_min_max.y - cloud_altitudes_min_max.x);
    float stepSize = (verticalThickness / float(SHADOW_MARCH_STEPS)) / dirYAbs;

    for (int i = 0; i < SHADOW_MARCH_STEPS; ++i)
    {
        if (length(rayPos) < cloud_altitudes_min_max.x)
        {
            break;
        }

        float3 weather = sample_weather_data(rayPos.xz);
        float density = sample_cloud_density(rayPos, weather, SHADOW_DENSITY_MIP_LEVEL, true);

        if (density > 0.0f)
        {
            totalDensity += density * stepSize;
        }

        rayPos += rayDir * stepSize;
    }

    float transmittance = exp(-totalDensity * density_scale * SHADOW_DENSITY_SCALE);
    return float4(transmittance, SHADOW_OUTPUT_BASE.y, SHADOW_OUTPUT_BASE.z, SHADOW_OUTPUT_BASE.w);
}