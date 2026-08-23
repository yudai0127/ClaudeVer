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

static const float PRECIP_START = 0.75;
static const float FBM_NORMALIZATION = 1.0 / 0.9375;
static const float MACRO_DETAIL_BLEND = 0.18;
static const float COVERAGE_THRESHOLD_HIGH = 0.76;
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

    // Separate large weather cells from a weak secondary breakup. The old
    // single FBM value was used directly as coverage and produced one broad,
    // connected cloud sheet. A thresholded macro field creates distinct cloud
    // groups with clear sky between them.
    float macroNoise = saturate(fbm((uv + flow) * noiseScale) * FBM_NORMALIZATION);
    float2 detailUv = uv * float2(1.37, 0.91) + flow * 1.31 + float2(0.19, 0.43);
    float detailNoise = saturate(fbm(detailUv * noiseScale * 2.35) * FBM_NORMALIZATION);
    float weatherSignal = lerp(macroNoise, detailNoise, MACRO_DETAIL_BLEND);

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
    float placement = smoothstep(threshold, threshold + edgeWidth, weatherSignal);

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
float typeVariation = (macroNoise - 0.5) * 0.22
                    + (detailNoise - 0.5) * 0.08;
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
        rain *= smoothstep(0.35, 0.72, placement);
        coverage = max(coverage, rain * requestedCoverage);
    }

    WeatherMapUAV[id.xy] = float4(coverage, rain, ctype, 1.0);
}
