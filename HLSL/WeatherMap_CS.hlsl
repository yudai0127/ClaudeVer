cbuffer WEATHER_GEN_CB : register(b0)
{
    float2 resolution;
    float time;
    float weatherT;

    float2 windDir;
    float windSpeed;
    float pad0;

    float sunnyCoverage;
    float rainyCoverage;
    float sunnyRain;
    float rainyRain;
    float sunnyType;
    float rainyType;
    float noiseScale;
    float noiseAmp;
};

RWTexture2D<float4> WeatherMapUAV : register(u0);

static const float HASH_SCALE = 0.1031;
static const float HASH_OFFSET = 33.33;

static const int FBM_OCTAVES = 4;
static const float FBM_GAIN = 0.5;
static const float FBM_LACUNARITY = 2.02;

static const float FLOW_TIME_SCALE = 0.02;
// The weather map spans roughly 200 km. These relative scales create a
// hierarchy of broad weather fronts, cloud groups and irregular boundaries
// instead of distributing equal-sized FBM cells uniformly across the sky.
static const float2 WEATHER_PATTERN_OFFSET = float2(0.07, 0.37);
static const float WEATHER_FRONT_SCALE = 0.18;
static const float WEATHER_CLUSTER_SCALE = 0.72;
static const float WEATHER_BOUNDARY_SCALE = 1.80;
static const float WEATHER_WARP_SCALE = 0.30;
static const float WEATHER_WARP_STRENGTH = 0.08;
static const float WEATHER_FRONT_WEIGHT = 0.55;
static const float WEATHER_CLUSTER_WEIGHT = 0.35;
static const float WEATHER_BOUNDARY_WEIGHT = 0.10;

static const float PRECIP_START = 0.75;
static const float RAIN_COVERAGE_THRESHOLD_BIAS = 0.04;
static const float RAIN_FIELD_FLOOR = 0.85;
static const float FBM_NORMALIZATION = 1.0 / 0.9375;
static const float COVERAGE_THRESHOLD_HIGH = 0.66;
static const float COVERAGE_THRESHOLD_LOW = 0.30;
static const float COVERAGE_EDGE_MIN = 0.10;
static const float COVERAGE_EDGE_MAX = 0.22;



float hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * HASH_SCALE);
    p3 += dot(p3, p3.yzx + HASH_OFFSET);
    return frac((p3.x + p3.y) * p3.z);
}

float noise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash12(i);
    float b = hash12(i + float2(1, 0));
    float c = hash12(i + float2(0, 1));
    float d = hash12(i + float2(1, 1));
    float2 u = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm(float2 p)
{
    float v = 0;
    float a = FBM_GAIN;
    [unroll]
    for (int i = 0; i < FBM_OCTAVES; i++)
    {
        v += a * noise(p);
        p *= FBM_LACUNARITY;
        a *= FBM_GAIN;
    }
    return v;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint) resolution.x || id.y >= (uint) resolution.y)
        return;

    float2 uv = (float2(id.xy) + 0.5) / resolution;

    float2 flow = (windDir * windSpeed) * time * FLOW_TIME_SCALE;
    float2 weatherUv = uv + WEATHER_PATTERN_OFFSET;

    // Domain-warp a low-frequency front so large cloud banks do not follow
    // obvious circular FBM contours.
    float2 baseWeatherUv = weatherUv + flow;
    float2 warpNoise = float2(
        fbm((baseWeatherUv + float2(0.17, 0.31))
            * noiseScale * WEATHER_WARP_SCALE),
        fbm((baseWeatherUv + float2(0.63, 0.11))
            * noiseScale * WEATHER_WARP_SCALE));
    warpNoise = saturate(warpNoise * FBM_NORMALIZATION);
    float2 warpedWeatherUv = baseWeatherUv
                           + (warpNoise - 0.5) * WEATHER_WARP_STRENGTH;

    // HZD-style hierarchy: broad fronts govern placement, medium cells form
    // distinct cloud groups, and the smallest field only breaks their edges.
    float macroNoise = saturate(
        fbm(warpedWeatherUv * noiseScale * WEATHER_FRONT_SCALE)
        * FBM_NORMALIZATION);
    float2 clusterUv = warpedWeatherUv * float2(1.19, 0.87)
                     + float2(0.41, 0.23);
    float clusterNoise = saturate(
        fbm(clusterUv * noiseScale * WEATHER_CLUSTER_SCALE)
        * FBM_NORMALIZATION);
    float2 detailUv = warpedWeatherUv * float2(0.91, 1.31)
                    + float2(0.19, 0.43);
    float detailNoise = saturate(
        fbm(detailUv * noiseScale * WEATHER_BOUNDARY_SCALE)
        * FBM_NORMALIZATION);
    float weatherSignal = macroNoise * WEATHER_FRONT_WEIGHT
                        + clusterNoise * WEATHER_CLUSTER_WEIGHT
                        + detailNoise * WEATHER_BOUNDARY_WEIGHT;

    float tCov = saturate(weatherT);
    float tType = saturate(weatherT);

    float tRain = saturate((weatherT - PRECIP_START) / (1.0 - PRECIP_START));

float requestedCoverage = saturate(lerp(sunnyCoverage, rainyCoverage, tCov)
                                 + lerp(0.05, 0.08, tCov));
float rain = lerp(sunnyRain, rainyRain, tRain);
float requestedType = saturate(lerp(sunnyType, rainyType, tType)
                             + tType * 0.04);

    float threshold = lerp(COVERAGE_THRESHOLD_HIGH,
                           COVERAGE_THRESHOLD_LOW,
                           requestedCoverage);
    float edgeWidth = lerp(COVERAGE_EDGE_MIN,
                           COVERAGE_EDGE_MAX,
                           requestedCoverage);
    // Close broad clear-sky holes only after precipitation begins. The 3D
    // Perlin-Worley field still erodes the rain layer, so it does not become a
    // featureless slab; this only prevents weather-map-sized blue openings.
    float placementThreshold = threshold
                             - tRain * RAIN_COVERAGE_THRESHOLD_BIAS;
    float placement = smoothstep(placementThreshold,
                                 placementThreshold + edgeWidth,
                                 weatherSignal);

    // Preserve a small amount of irregularity without rejoining neighbouring
    // cells. noiseAmp remains part of the existing CPU/UI data contract.
    float boundaryDetail = (detailNoise - 0.5) * noiseAmp * 0.35;
    placement = saturate(placement + boundaryDetail * placement * (1.0 - placement));

    // HZD uses the weather-map cloud type to select a vertical density
    // profile. Make that type spatial: a cloud-cell core grows into cumulus,
    // while its perimeter collapses toward lower stratocumulus. Keeping one
    // nearly constant type across the whole cell produces a flat cloud slab
    // when viewed at grazing angles.
float cellCore = smoothstep(0.08, 0.84, placement);
float ctype = lerp(0.26, requestedType, cellCore);
float typeVariation = (clusterNoise - 0.5) * 0.24
                    + (macroNoise - 0.5) * 0.12
                    + (detailNoise - 0.5) * 0.04;
    ctype = saturate(ctype + typeVariation * lerp(0.25, 1.0, cellCore));

    // Store actual local coverage rather than a binary placement mask. The
    // volumetric shader uses this value to shift its density threshold.
    float coverage = placement * requestedCoverage;

    if (tRain <= 0.0)
    {
        rain = 0.0;
    }
    else
    {
        float rainMask = smoothstep(0.35, 0.72, placement);
        // At full Rainy, precipitation represents a connected storm system.
        // Keep spatial variation, but do not let weak placement cells disable
        // the rain-only density floor and reopen large blue-sky holes.
        rainMask = lerp(rainMask,
                        max(rainMask, RAIN_FIELD_FLOOR),
                        tRain);
        rain *= rainMask;
        coverage = max(coverage, rain * requestedCoverage);
    }

    WeatherMapUAV[id.xy] = float4(coverage, rain, ctype, 1.0);
}
