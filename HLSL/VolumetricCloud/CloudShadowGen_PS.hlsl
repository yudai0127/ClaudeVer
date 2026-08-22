#include "VolumetricCloud.hlsli"

static const float WORLD_RANGE = 20000.0f;
static const int SHADOW_MARCH_STEPS = 16;
static const float SHADOW_DENSITY_MIP_LEVEL = 3.0f;
static const float SHADOW_DENSITY_SCALE = 2.0f;

static const float4 SHADOW_OUTSIDE_COLOR = float4(1.0f, 0.0f, 0.0f, 1.0f);
static const float4 SHADOW_OUTPUT_BASE = float4(0.0f, 0.0f, 0.0f, 1.0f);

float4 main(VS_OUT pin) : SV_TARGET
{
    float2 worldXZ = (pin.texcoord - 0.5f) * WORLD_RANGE + camera_position.xz;

    float cloudPlanetRadius = get_cloud_planet_radius();
    float earthRadiusWithCloud = cloudPlanetRadius + cloud_altitudes_min_max.y;
    float distSq = dot(worldXZ, worldXZ);
    float radiusSq = earthRadiusWithCloud * earthRadiusWithCloud;
    if (distSq > radiusSq)
    {
        return SHADOW_OUTSIDE_COLOR;
    }

    // 太陽方向にレイを飛ばし、角度に応じてステップ長を補正
    float3 lightDir = normalize(sunDirection.xyz);
    float surfaceRadiusSq = cloudPlanetRadius * cloudPlanetRadius;
    if (distSq >= surfaceRadiusSq)
    {
        return SHADOW_OUTSIDE_COLOR;
    }
    float surfaceY = sqrt(surfaceRadiusSq - distSq);
    float3 surfacePos = float3(worldXZ.x, surfaceY, worldXZ.y);
    float3 rayDir = lightDir;

    float totalDensity = 0.0f;
    float bottomRadius = cloudPlanetRadius + cloud_altitudes_min_max.x;
    float topRadius = cloudPlanetRadius + cloud_altitudes_min_max.y;
    float innerNear = 0.0f;
    float innerFar = 0.0f;
    float outerNear = 0.0f;
    float outerFar = 0.0f;
    if (!intersect_sphere_range(surfacePos, rayDir, bottomRadius, innerNear, innerFar)
        || !intersect_sphere_range(surfacePos, rayDir, topRadius, outerNear, outerFar))
    {
        return SHADOW_OUTSIDE_COLOR;
    }

    float startT = max(innerFar, 0.0f);
    float endT = max(outerFar, startT);
    float stepSize = (endT - startT) / float(SHADOW_MARCH_STEPS);
    float3 rayPos = surfacePos + rayDir * (startT + stepSize * 0.5f);

    for (int i = 0; i < SHADOW_MARCH_STEPS; ++i)
    {
        float radius = length(rayPos);
        if (radius < bottomRadius || radius > topRadius)
        {
            rayPos += rayDir * stepSize;
            continue;
        }

        float3 weather = sample_weather_data(rayPos.xz);
        float density = sample_cloud_density(rayPos, weather, SHADOW_DENSITY_MIP_LEVEL, true);

        if (density > 0.0f)
        {
            totalDensity += density * stepSize;
        }

        rayPos += rayDir * stepSize;
    }

    float verticalThickness = max(cloud_altitudes_min_max.y - cloud_altitudes_min_max.x, 1.0f);
    float normalizedDensity = totalDensity / verticalThickness;
    float transmittance = exp(-normalizedDensity * density_scale * SHADOW_DENSITY_SCALE);
    return float4(transmittance, SHADOW_OUTPUT_BASE.y, SHADOW_OUTPUT_BASE.z, SHADOW_OUTPUT_BASE.w);
}
