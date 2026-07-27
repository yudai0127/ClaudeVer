#include "../Sampler.hlsli"
#define ENABLE_HIT_BUFFER 1

struct Particle
{
    float3 pos;
    float life;
    float3 vel;
    float seed;
};

// UAV / SRV
RWStructuredBuffer<Particle> gParticles : register(u0);

#if ENABLE_HIT_BUFFER
AppendStructuredBuffer<float4> gHitBuffer : register(u1);
#endif

Texture2D<float4> gWeatherTex : register(t0);

cbuffer RainCB : register(b0)
{
    float4x4 gView;
    float4x4 gProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;

    float3 gCameraPos;
    float gDt;

    float3 gWind;
    float gTime;

    float3 gSpawnCenter;
    float gSpawnRadius;

    float gSpawnTopY;
    float gKillBottomY;
    float gGravity;
    float gWaterY;

    float2 gNearFar;
    float gSoftRange;
    float gRainIntensity;

    float3 gCameraVel;
    float gPad0;
};

static const float WEATHER_UV_SCALE = 0.00006;
static const float TIME_OFFSET = 10000.0;
static const float WEATHER_TIME_SCALE = 0.001;
static const float UV_CENTER = 0.5;

static const float RAIN_ON_TH = 0.20;
static const float RAIN_KEEP_TH = 0.15;

static const int SPAWN_TRIES = 8;

static const float FALL_MIN = 16.0;
static const float FALL_MAX = 32.0;

static const float LIFE_MIN = 1.4;
static const float LIFE_MAX = 2.6;

static const float HASH11_SCALE = 43758.5453;
static const float HASH21_X_MUL = 1.123;
static const float HASH21_X_ADD = 17.3;
static const float HASH21_Y_MUL = 2.271;
static const float HASH21_Y_ADD = 91.7;

static const float TWO_PI = 6.2831853;
static const float SPAWN_Y_JITTER_MIN = -12.0;
static const float SPAWN_Y_JITTER_MAX = 12.0;
static const float SPAWN_Y_JITTER_SEED_OFFSET = 6.6;

static const float GUST_TIME_FREQ = 0.6;
static const float GUST_SEED_FREQ = 0.1;
static const float GUST_BIAS = 0.5;
static const float GUST_AMP = 0.5;
static const float2 WIND_POS_NOISE_SCALE = float2(0.07, 0.09);
static const float WIND_TIME_NOISE_FREQ = 2.0;
static const float WIND_BASE_MIN = 0.7;
static const float WIND_BASE_MAX = 1.3;
static const float WIND_TURB_SCALE = 0.8;

static const float BASE_SEED_FROM_ID_SCALE = 13.37;
static const float BASE_SEED_TIME_SCALE = 0.1;
static const float TRY_SEED_STEP = 19.19;

static const float FALL_SEED_OFFSET = 9.1;
static const float JITTER_SEED_OFFSET = 2.2;
static const float LIFE_SEED_OFFSET = 4.7;

static const float JITTER_MIN = -1.0;
static const float JITTER_MAX = 1.0;
static const float VELOCITY_JITTER_SCALE = 0.25;

static const float LIFE_RAIN_BASE = 0.85;
static const float LIFE_RAIN_SCALE = 0.35;

static const float WIND_FORCE_DT_SCALE = 0.15;
static const float HIT_STRENGTH_DENOM = 40.0;

float Hash11(float n)
{
    return frac(sin(n) * HASH11_SCALE);
}

float2 Hash21(float n)
{
    float x = Hash11(n * HASH21_X_MUL + HASH21_X_ADD);
    float y = Hash11(n * HASH21_Y_MUL + HASH21_Y_ADD);
    return float2(x, y);
}

float SampleRain(float2 worldXZ)
{
    float2 windXZ = float2(gWind.x, gWind.z);
    float2 offset = ((gTime + TIME_OFFSET) * WEATHER_TIME_SCALE) * (-windXZ);

    float2 uv = worldXZ * WEATHER_UV_SCALE + UV_CENTER + offset;
    return gWeatherTex.SampleLevel(sampler_states[LINEAR_MIRROR], uv, 0).g;
}

float3 SpawnPosFromSeed(float seed)
{
    float2 r = Hash21(seed);
    float a = r.x * TWO_PI;
    float rad = sqrt(r.y) * gSpawnRadius;

    float3 p;
    p.x = gSpawnCenter.x + cos(a) * rad;
    p.z = gSpawnCenter.z + sin(a) * rad;

    float yJit = lerp(SPAWN_Y_JITTER_MIN, SPAWN_Y_JITTER_MAX, Hash11(seed + SPAWN_Y_JITTER_SEED_OFFSET));
    p.y = gSpawnTopY + yJit;

    return p;
}

float3 ComputeWind(float seed, float3 worldPos)
{
    float gust = sin(gTime * GUST_TIME_FREQ + seed * GUST_SEED_FREQ) * GUST_AMP + GUST_BIAS;
    float2 t = Hash21(seed + dot(worldPos.xz, WIND_POS_NOISE_SCALE) + gTime * WIND_TIME_NOISE_FREQ) - UV_CENTER;

    float3 base = float3(gWind.x, 0, gWind.z) * lerp(WIND_BASE_MIN, WIND_BASE_MAX, gust);
    float3 turb = float3(t.x, 0, t.y) * WIND_TURB_SCALE;

    return base + turb;
}

[numthreads(256, 1, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint id = dtid.x;
    Particle p = gParticles[id];

    if (p.life <= 0.0f)
    {
        float baseSeed = (p.seed != 0.0f) ? p.seed : ((id + 1) * BASE_SEED_FROM_ID_SCALE);
        baseSeed += gTime * BASE_SEED_TIME_SCALE;

        float3 spawnPos = 0;
        float rainMask = 0;

        [unroll]
        for (int i = 0; i < SPAWN_TRIES; ++i)
        {
            float trySeed = baseSeed + i * TRY_SEED_STEP;
            spawnPos = SpawnPosFromSeed(trySeed);
            rainMask = SampleRain(spawnPos.xz);

            if (rainMask >= RAIN_ON_TH)
                break;
        }

        if (rainMask < RAIN_ON_TH)
        {
            p.life = 0.0f;
            p.seed = baseSeed;
            gParticles[id] = p;
            return;
        }

        float r01 = saturate((rainMask - RAIN_ON_TH) / (1.0 - RAIN_ON_TH));

        p.pos = spawnPos;

        float fallSpeed = lerp(FALL_MIN, FALL_MAX, Hash11(baseSeed + FALL_SEED_OFFSET));
        float jitter = lerp(JITTER_MIN, JITTER_MAX, Hash11(baseSeed + JITTER_SEED_OFFSET));

        float3 wind = ComputeWind(baseSeed, spawnPos);

        p.vel = float3(wind.x, -fallSpeed, wind.z) + float3(jitter, 0, -jitter) * VELOCITY_JITTER_SCALE;
        p.life = lerp(LIFE_MIN, LIFE_MAX, Hash11(baseSeed + LIFE_SEED_OFFSET)) * (LIFE_RAIN_BASE + LIFE_RAIN_SCALE * r01);
        p.seed = baseSeed;

        gParticles[id] = p;
        return;
    }

    float rainNow = SampleRain(p.pos.xz);
    if (rainNow < RAIN_KEEP_TH)
    {
        p.life = 0.0f;
        gParticles[id] = p;
        return;
    }

    p.vel += float3(0, gGravity, 0) * gDt;

    float3 wind = ComputeWind(p.seed, p.pos);
    p.vel += wind * (WIND_FORCE_DT_SCALE * gDt);

    p.pos += p.vel * gDt;
    p.life -= gDt;

#if ENABLE_HIT_BUFFER
    if (p.pos.y <= gWaterY)
    {
        float strength = saturate((-p.vel.y) / HIT_STRENGTH_DENOM);
        gHitBuffer.Append(float4(p.pos.xyz, strength));
        p.life = 0.0f;
    }
#endif

    if (p.pos.y <= gKillBottomY)
        p.life = 0.0f;

    gParticles[id] = p;
}