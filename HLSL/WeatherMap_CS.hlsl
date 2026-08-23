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



static const float2 WEATHER_PATTERN_OFFSET = float2(0.07, 0.37);
static const float WEATHER_FRONT_SCALE = 0.18;
static const float WEATHER_CLUSTER_SCALE = 1.15;
static const float WEATHER_BOUNDARY_SCALE = 2.40;
static const float WEATHER_WARP_SCALE = 0.30;
static const float WEATHER_WARP_STRENGTH = 0.08;
static const float WEATHER_FRONT_WEIGHT = 0.35;
static const float WEATHER_CLUSTER_WEIGHT = 0.50;
static const float WEATHER_BOUNDARY_WEIGHT = 0.15;

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



    float2 baseWeatherUv = weatherUv + flow;
    float2 warpNoise = float2(
        fbm((baseWeatherUv + float2(0.17, 0.31))
            * noiseScale * WEATHER_WARP_SCALE),
        fbm((baseWeatherUv + float2(0.63, 0.11))
            * noiseScale * WEATHER_WARP_SCALE));
    warpNoise = saturate(warpNoise * FBM_NORMALIZATION);
    float2 warpedWeatherUv = baseWeatherUv
                           + (warpNoise - 0.5) * WEATHER_WARP_STRENGTH;



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




    float tCov = saturate(weatherT * weatherT);
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



    float placementThreshold = threshold
                             - tRain * RAIN_COVERAGE_THRESHOLD_BIAS;
    float placement = smoothstep(placementThreshold,
                                 placementThreshold + edgeWidth,
                                 weatherSignal);





    float fairWeatherCell = smoothstep(0.50, 0.68, clusterNoise);
    float fairWeatherSeparation = lerp(0.01, 1.0, fairWeatherCell);
    placement *= lerp(fairWeatherSeparation, 1.0, tRain);



    float boundaryDetail = (detailNoise - 0.5) * noiseAmp * 0.35;
    placement = saturate(placement + boundaryDetail * placement * (1.0 - placement));





    float cellCore = smoothstep(0.16, 0.82, placement);
    float typeField = clusterNoise * 0.68
                    + macroNoise * 0.20
                    + detailNoise * 0.12;





    float middleCloud = smoothstep(0.40, 0.62, typeField);
    float towerCore = smoothstep(0.54, 0.76, typeField) * cellCore;






    float lowCloudType = lerp(0.48, 0.58, macroNoise);
    float middleCloudType = lerp(0.58, 0.78, clusterNoise);
    float highCloudType = saturate(requestedType + 0.18);

    float ctype = lerp(lowCloudType, middleCloudType, middleCloud);
    ctype = lerp(ctype, highCloudType, towerCore);
    ctype = lerp(0.48, ctype, cellCore);

    float typeVariation = (detailNoise - 0.5) * 0.04;
    ctype = saturate(ctype + typeVariation * cellCore);
    ctype = lerp(ctype, max(ctype, requestedType), tRain);



    float coverage = placement * requestedCoverage;

    if (tRain <= 0.0)
    {
        rain = 0.0;
    }
    else
    {
        float rainMask = smoothstep(0.35, 0.72, placement);



        rainMask = lerp(rainMask,
                        max(rainMask, RAIN_FIELD_FLOOR),
                        tRain);
        rain *= rainMask;
        coverage = max(coverage, rain * requestedCoverage);
    }

    WeatherMapUAV[id.xy] = float4(coverage, rain, ctype, 1.0);
}
