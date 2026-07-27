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
static const float PATCH_CENTER = 0.5;
static const float PATCH_SCALE = 2.0;
static const float RAIN_PATCH_SCALE = 0.8;



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
    float n = fbm((uv + flow) * noiseScale);

    float tCov = saturate(weatherT);
    float tType = saturate(weatherT);

    float tRain = saturate((weatherT - PRECIP_START) / (1.0 - PRECIP_START));

    float coverage = lerp(sunnyCoverage, rainyCoverage, tCov);
    float rain = lerp(sunnyRain, rainyRain, tRain);
    float ctype = lerp(sunnyType, rainyType, tType);

    float patch = (n - PATCH_CENTER) * PATCH_SCALE;
    coverage = saturate(coverage + patch * noiseAmp);

    if (tRain <= 0.0)
    {
        rain = 0.0;
    }
    else
    {
        rain = saturate(rain + patch * noiseAmp * RAIN_PATCH_SCALE);
        coverage = max(coverage, rain);
    }

    WeatherMapUAV[id.xy] = float4(coverage, rain, ctype, 1.0);
}
