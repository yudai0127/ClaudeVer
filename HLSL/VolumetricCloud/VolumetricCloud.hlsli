#define PI 3.14159265358979
#include "../Atmosphere.hlsli"
#include "../Sampler.hlsli"

struct VS_OUT
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD;
};

cbuffer SCENE_CONSTANT_BUFFER : register(b1)
{
    row_major float4x4 view_projection;
    float4 camera_position;
    row_major float4x4 inverse_view_projection;
}


cbuffer VOLUMETRIC_CLOUD_CONSTANT_BUFFER : register(b8)
{
    float2 wind_direction;
    float2 cloud_altitudes_min_max; // 雲レイヤー下端/上端

    float wind_speed;

    float density_scale; 
    float cloud_coverage_scale; 
    float rain_cloud_absorption_scale; // 雨雲の吸収の強さ
    float cloud_type_scale; 

    float horizon_distance_scale;
    float low_frequency_perlin_worley_sampling_scale;
    float high_frequency_worley_sampling_scale;
    float cloud_density_long_distance_scale;
    bool enable_powdered_sugar_efffect;

    uint ray_marching_steps; //レイマーチステップ
    bool auto_ray_marching_steps; 

    float time;
}





TextureCube skybox : register(t0);
Texture3D<float4> low_frequency_perlin_worley_texture : register(t1);
Texture3D<float3> high_frequency_worley_texture : register(t2);

Texture2D<float4> weather_texture : register(t3);
Texture2D<float3> curl_noise_texture : register(t4);
Texture2D transmittance_texture : register(t5);
TextureCube irradiance_texture : register(t6);

static const float time_offset = 10000.0;

static const float WEATHER_SPEED_CAP = 0.10;
static const float WEATHER_UV_TIME_SCALE = 0.0015;
static const float MIN_ALTITUDE_FROM_GROUND = 1.0;
static const float MIN_HORIZON_DISTANCE = 512.0;

static const float ANIMATION_TIME_WRAP = 4096.0;
static const float LOW_FREQ_WIND_ANIM_SCALE = 600.0;
static const float HIGH_FREQ_WIND_ANIM_SCALE = 900.0;
static const float ANIMATION_UV_WRAP = 2.0;
static const float CURL_NOISE_UV_SCALE = 0.00008;

static const int CONE_SAMPLE_COUNT = 4;
static const float CONE_LATERAL_SCALE = 0.35;
static const float LONG_DISTANCE_DENSITY_MIP = 5.0;

static const float SUN_VISIBILITY_START_Y = -0.02;
static const float SUN_VISIBILITY_END_Y = 0.06;
static const float SUNSET_HORIZON_BAND_END = 0.22;

static const float BASE_MIP_MAX = 2.5;
static const float STEP_BIAS_START = 0.75;
static const float STEP_BIAS_RANGE = 2.5;
static const float STEP_BIAS_SCALE = 1.5;
static const float MAX_DENSITY_MIP = 4.0;

static const float JITTER_HASH_SCALE = 10.0;
static const float PHASE_G = 0.1;
static const float CONE_SPREAD_DIVISOR = 36.0;
static const float SEGMENT_STEP_EPSILON = 1e-5;
static const int ZERO_DENSITY_RESET_COUNT = 6;
static const float TRANSMITTANCE_EARLY_OUT = 0.01;

static const float RAIN_ABSORPTION_MIN = 0.05;
static const float RAIN_ABSORPTION_RAININESS_SCALE = 2.0;
static const float DIRECT_OCCLUSION_MIN = 0.18;
static const float AMBIENT_OCCLUSION_OVERCAST_MIN = 0.45;
static const float AMBIENT_OCCLUSION_RAIN_MIN = 0.70;
static const float POWDERED_SUGAR_THICKNESS_SCALE = 2.0;
static const float AMBIENT_SUNSET_BLEND = 0.6;

static const float NIGHT_AMBIENT_MIN_FACTOR = 0.08;
static const float DIRECT_VISIBILITY_POWER = 2.0;
// remap
float remap(float original_value, float original_min, float original_max, float new_min, float new_max)
{
    float t = saturate((original_value - original_min) / (original_max - original_min));
    return new_min + t * (new_max - new_min);
}

float2 normalize_safe(float2 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8)
    {
        return float2(1.0, 0.0);
    }
    return v * rsqrt(len2);
}

// 低周波ノイズ(Perlin-Worley)をサンプル
float4 sample_low_frequency_noises(float3 sample_point, float mip_level)
{
    return low_frequency_perlin_worley_texture.SampleLevel(
        sampler_states[LINEAR_MIRROR],
        sample_point * low_frequency_perlin_worley_sampling_scale,
        mip_level
    );
}


// 高周波ノイズ(Worley)をサンプル
float3 sample_high_frequency_noises(float3 sample_point, float mip_level)
{
    return high_frequency_worley_texture.SampleLevel(
        sampler_states[LINEAR_MIRROR],
        sample_point * high_frequency_worley_sampling_scale,
        mip_level
    );
}





float3 sample_weather_data(float2 sample_point)
{
    float2 wind_dir = normalize_safe(-wind_direction);

    float weather_speed = min(wind_speed, WEATHER_SPEED_CAP);
    float2 offset = frac(wind_dir * (time * weather_speed * WEATHER_UV_TIME_SCALE));

    // 低高度での不安定化を避けるため、地表からの高度で計算
    float cloud_base_altitude = max(cloud_altitudes_min_max.x, MIN_ALTITUDE_FROM_GROUND);
    float horizon_distance = sqrt(max(cloud_base_altitude * (cloud_base_altitude + 2.0 * planetRadius), MIN_ALTITUDE_FROM_GROUND)) * horizon_distance_scale;
    horizon_distance = max(horizon_distance, MIN_HORIZON_DISTANCE);

    float2 mapped = float2(sample_point.x + horizon_distance, horizon_distance - sample_point.y) / (2.0 * horizon_distance);

    float2 uv = frac(mapped + offset);

    return weather_texture.SampleLevel(
        sampler_states[LINEAR_MIRROR],
        uv,
        0
    ).rgb;
}




// 雲レイヤー内の高さを計算
float get_height_fraction_for_point(float position)
{
    float height_fraction = (position - cloud_altitudes_min_max.x) / (cloud_altitudes_min_max.y - cloud_altitudes_min_max.x);
    return clamp(height_fraction, 0.0, 1.0);
}


float get_height_fraction_for_point_radius(float radius)
{
    float altitude = radius - planetRadius;
    float hf = (altitude - cloud_altitudes_min_max.x) / (cloud_altitudes_min_max.y - cloud_altitudes_min_max.x);
    return saturate(hf);
}

// 高さ方向の密度カーブを計算
float get_density_height_gradient(float height_fraction, float cloud_type)
{
    const float4 stratus_gradient = float4(0.02f, 0.05f, 0.09f, 0.11f);
    const float4 stratocumulus_gradient = float4(0.02f, 0.2f, 0.48f, 0.625f);
    const float4 cumulus_gradient = float4(0.01f, 0.0625f, 0.78f, 1.0f);

   
    float stratus = 1.0f - clamp(cloud_type * 2.0f, 0.0, 1.0);
    float stratocumulus = 1.0f - abs(cloud_type - 0.5f) * 2.0f;
    float cumulus = clamp(cloud_type - 0.5f, 0.0, 1.0) * 2.0f;

    float4 cloud_gradient = stratus_gradient * stratus
                          + stratocumulus_gradient * stratocumulus
                          + cumulus_gradient * cumulus;

    
    return smoothstep(cloud_gradient.x, cloud_gradient.y, height_fraction)
         - smoothstep(cloud_gradient.z, cloud_gradient.w, height_fraction);
}


#define ENABLE_CLOUD_ANIMATION

float sample_cloud_density(float3 sample_point, float3 weather_data, float mip_level, bool cheap_sample)
{
    float height_fraction = get_height_fraction_for_point_radius(length(sample_point));

#ifdef ENABLE_CLOUD_ANIMATION
    float2 wind_dir = normalize_safe(-wind_direction);
    float anim_time = fmod(time, ANIMATION_TIME_WRAP);

    float3 low_freq_point = sample_point;
    float2 low_uv_offset = wind_dir * (anim_time * wind_speed * LOW_FREQ_WIND_ANIM_SCALE
                           * low_frequency_perlin_worley_sampling_scale);
    low_freq_point.xz += fmod(low_uv_offset, ANIMATION_UV_WRAP)
                         / low_frequency_perlin_worley_sampling_scale;
#else
    float3 low_freq_point = sample_point;
#endif

    float4 low_frequency_noises = sample_low_frequency_noises(low_freq_point, mip_level - 2.0);
    float low_frequency_fbm = low_frequency_noises.g * 0.625 + low_frequency_noises.b * 0.25 + low_frequency_noises.a * 0.125;

    float base_cloud = remap(low_frequency_noises.r, -(1.0 - low_frequency_fbm), 1.0, 0.0, 1.0);
    float cloud_type = clamp(weather_data.b * cloud_type_scale, 0.0, 1.0);

    float density_height_gradient = get_density_height_gradient(height_fraction, cloud_type);
    base_cloud *= density_height_gradient;

    float cloud_coverage = weather_data.r * cloud_coverage_scale;
    float base_cloud_with_coverage = remap(base_cloud, 1.0 - cloud_coverage, 1.0, 0.0, 1.0);
    base_cloud_with_coverage *= cloud_coverage;

    float final_cloud = base_cloud_with_coverage;

    if (!cheap_sample && base_cloud_with_coverage > 0.0)
    {
#ifdef ENABLE_CLOUD_ANIMATION
        float3 detail_point = sample_point;
        float2 detail_uv_offset = -wind_dir * (anim_time * wind_speed * HIGH_FREQ_WIND_ANIM_SCALE
                                  * high_frequency_worley_sampling_scale);
        detail_point.xz += fmod(detail_uv_offset, ANIMATION_UV_WRAP)
                           / high_frequency_worley_sampling_scale;
#else
        float3 detail_point = sample_point;
#endif

        float3 curl_noise = curl_noise_texture.SampleLevel(sampler_states[LINEAR_MIRROR], detail_point.xy * CURL_NOISE_UV_SCALE, 0);
        detail_point.xy += curl_noise.xy * (1.0 - height_fraction);

        float3 high_frequency_noises = sample_high_frequency_noises(detail_point, mip_level);
        float high_frequency_fbm = high_frequency_noises.r * 0.625 + high_frequency_noises.g * 0.25 + high_frequency_noises.b * 0.125;

        float high_frequency_noise_modifier = lerp(high_frequency_fbm, 1.0 - high_frequency_fbm, clamp(height_fraction * 4.0, 0.0, 1.0));
        final_cloud = remap(base_cloud_with_coverage, high_frequency_noise_modifier * 0.4 * height_fraction, 1.0, 0.0, 1.0);
    }

    return pow(clamp(final_cloud, 0.0, 1.0), (1.0 - height_fraction) * 0.8 + 0.5);
}


// 雲の明るさ全体スケール
static const float CLOUD_LIGHT_SCALE = 0.7;


// hash:ジッタリングに使う乱数
float hash(float3 p)
{
    p = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}


// Henyey-Greenstein 位相関数: 光の散乱の向き(前方散乱/後方散乱)を表す
float henyey_greenstein(float cos_theta, float g)
{
    return (1.0 - g * g) / (pow(1.0 + g * g - 2.0 * g * cos_theta, 1.50) * 4 * PI);
}


// 球との交差距離
float intersect_sphere(float3 pos, float3 dir, float r)
{
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, pos);
    float c = dot(pos, pos) - (r * r);
    float disc = (b * b) - 4.0 * a * c;
    
    if (disc < 0.0)
        return 0.0; // 交差なし（カメラが球の外で方向が外向き）
    
    float d = sqrt(disc);
    float p = (-b - d) / (2.0 * a);
    float p2 = (-b + d) / (2.0 * a);
    
    // カメラが球の内側なら近い正の値、外側なら遠い正の値
    if (p < 0.0)
        return max(0.0, p2);
    return p; // 外側から正しく前方の交差点を返す
}

bool intersect_sphere_range(float3 pos, float3 dir, float r, out float t_near, out float t_far)
{
    float a = dot(dir, dir);
    float b = 2.0 * dot(dir, pos);
    float c = dot(pos, pos) - (r * r);
    float disc = (b * b) - 4.0 * a * c;

    if (disc < 0.0)
    {
        t_near = 0.0;
        t_far = 0.0;
        return false;
    }

    float d = sqrt(disc);
    float inv_2a = 0.5 / a;
    t_near = (-b - d) * inv_2a;
    t_far = (-b + d) * inv_2a;

    if (t_near > t_far)
    {
        float tmp = t_near;
        t_near = t_far;
        t_far = tmp;
    }

    return true;
}


#define ENABLE_CLOUD_ANIMATION


// 光用のコーン(円錐)サンプルで使うオフセット方向
static const float3 noise_kernel[6] =
{
    float3(0.38051305f, 0.92453449f, -0.02111345f),
    float3(-0.50625799f, -0.03590792f, -0.86163418f),
    float3(-0.32509218f, -0.94557439f, 0.01428793f),
    float3(0.09026238f, -0.27376545f, 0.95755165f),
    float3(0.28128598f, 0.42443639f, -0.86065785f),
    float3(-0.16852403f, 0.14748697f, 0.97460106f)
};


// 太陽方向へ密度を集めて、自己影を近似
float sample_cloud_density_along_cone(float3 ray_origin, float3 ray_direction, float cone_spread_multplier)
{
    float density_along_cone = 0.0;

    const float stepLen = cone_spread_multplier;

    [unroll]
    for (int i = 0; i < CONE_SAMPLE_COUNT; i++)
    {
        float t = (i + 1) * stepLen;

        // 距離が伸びるほどコーンが太くなる
        float3 lateral = noise_kernel[i] * (t * CONE_LATERAL_SCALE);
        float3 sample_point = ray_origin + ray_direction * t + lateral;

        float3 weather_data = sample_weather_data(sample_point.xz);
        density_along_cone += sample_cloud_density(sample_point, weather_data, float(i), false);
    }

    return density_along_cone;
}


// 遠距離1点の密度サンプル
float sample_cloud_density_long_distance(float3 ray_origin, float3 ray_direction, float cone_spread_multplier)
{
    const float long_distance = cloud_density_long_distance_scale * cone_spread_multplier;

    float3 sample_point = ray_origin + ray_direction * long_distance;
    float3 weather_data = sample_weather_data(sample_point.xz);

    return sample_cloud_density(sample_point, weather_data, LONG_DISTANCE_DENSITY_MIP, false);
}


// 大気散乱の Transmittance LUT から「太陽光がどれだけ減衰したか」を取得
float3 GetSunTransmittance(float height, float sunZenithCos)
{
    // LUT作成時と同じマッピングでUV計算
    float u = (sunZenithCos + 1.0) * 0.5;
    float v = (height - planetRadius) / (atmosphereRadius - planetRadius);
    v = saturate(v);
    u = saturate(u);

   
    return transmittance_texture.SampleLevel(sampler_states[ClampLinear], float2(u, v), 0).rgb;
}



float get_sun_visibility(float sun_y)
{
    // 太陽が地平線より十分下なら 0、上なら 1 へ
    return smoothstep(SUN_VISIBILITY_START_Y, SUN_VISIBILITY_END_Y, sun_y);
}

float3 get_sunset_tint(float sun_y)
{
    // 地平線付近だけ夕焼け色（昼/夜は 1.0 に戻す）
    float horizon_band = 1.0 - smoothstep(0.0, SUNSET_HORIZON_BAND_END, abs(sun_y));
    float sun_visibility = get_sun_visibility(sun_y);
    float sunset = horizon_band * sun_visibility;

    const float3 warm_tint = float3(1.0, 0.58, 0.34);
    return lerp(1.0.xxx, warm_tint, sunset);
}

//レイ進行状況 + その区間の実ステップ長からLODを決める
float compute_density_mip(float t01, float segment_step_size)
{
    // 既存の距離方向ミップ
    float base_mip = lerp(0.0, BASE_MIP_MAX, t01);

    // ステップが粗いほどミップを上げる（エイリアシング抑制）
    float step_bias = saturate((segment_step_size - STEP_BIAS_START) / STEP_BIAS_RANGE) * STEP_BIAS_SCALE;

    return clamp(base_mip + step_bias, 0.0, MAX_DENSITY_MIP);
}


float4 ray_march(float3 ray_origin, float3 ray_step, int steps)
{
    float step_size = length(ray_step);

    // ジッタリング(バンディング対策)
#if 1
    // 各ピクセルでサンプル開始位置を少しずらしてバンディングを減らす
    float jitter = hash(ray_origin * JITTER_HASH_SCALE);
    float3 sample_point = ray_origin;
#else
    float jitter = 0.0;
    float3 sample_point = ray_origin;
#endif

    // 太陽方向と位相関数(散乱の向き)を準備
    float3 sun_direction = normalize(sunDirection.xyz);
    float3 view_dir = normalize(ray_step);
    float cos_theta = dot(sun_direction, -view_dir);

    // 位相関数(Phase Function)の計算
#if 1
    // g: 前方散乱の強さ(0=等方、正で前方散乱が強い)
    float g = PHASE_G;
    float henyey_greenstein_phase = henyey_greenstein(cos_theta, g);
#else
    float henyey_greenstein_phase = max(max(henyey_greenstein(cos_theta, 0.6), henyey_greenstein(cos_theta, (0.4 - 1.4 * sun_direction.y))), henyey_greenstein(cos_theta, -0.2));
#endif

    const float cone_spread_multplier = ((cloud_altitudes_min_max.y - cloud_altitudes_min_max.x) / CONE_SPREAD_DIVISOR);

    float3 color = 0.0;
    float density = 0;
    // transmittence: カメラ→現在地点までの透過率(1=透明, 0=不透明)
    float transmittence = 1.0;
    float cloud_test = 0;
    int zero_density_sample_count = 0;
    float prev_t_non_linear = 0.0;

    [loop]
    for (int i = 0; i < steps; i++)
    {
        float t01 = (float) i / max((float) (steps - 1), 1.0);
        // 非線形サンプリング
        float t_linear = ((float) i + jitter) / max((float) steps, 1.0);
        t_linear = saturate(t_linear);
        float t_non_linear = t_linear * t_linear;
        sample_point = ray_origin + ray_step * (t_non_linear * (float) steps);

        float segment_step_size = step_size * max((t_non_linear - prev_t_non_linear) * (float) steps, SEGMENT_STEP_EPSILON);
        float density_mip = compute_density_mip(t01, segment_step_size);

        if (cloud_test > 0.0)
        {
            float3 weather_data = sample_weather_data(sample_point.xz);
            float sampled_density = sample_cloud_density(sample_point, weather_data, density_mip, false);

            if (sampled_density == 0.0)
            {
                zero_density_sample_count++;
            }

            if (zero_density_sample_count != ZERO_DENSITY_RESET_COUNT)
            {
                density += sampled_density;

                if (sampled_density != 0.0)
                {
                    
                    float step_transmittance = exp(-density_scale * sampled_density * segment_step_size);
                    // 現在地点の高度・上方向ベクトルを作って、大気LUTから太陽光減衰を取る
                    float current_height = length(sample_point);
                    float3 up_vector = sample_point / current_height;
                    float sun_zenith_cos = dot(up_vector, sun_direction);

                    // transmittance_texture から、太陽光が大気を通ってどれだけ弱まったか
                    float3 sun_transmittance = GetSunTransmittance(current_height, sun_zenith_cos);
                    float3 incoming_sun_light = sunIntensity * sun_transmittance * CLOUD_LIGHT_SCALE;

                    float3 sky_irradiance = irradiance_texture.SampleLevel(sampler_states[ClampLinear], up_vector, 0).rgb;
                    // 太陽方向へ密度を集める → 自己影を近似
                    float density_along_light_ray = 0.0;
                    density_along_light_ray += sample_cloud_density_along_cone(sample_point, sun_direction, cone_spread_multplier);
                    density_along_light_ray += sample_cloud_density_long_distance(sample_point, sun_direction, cone_spread_multplier);

                    float optical_thickness = density_scale * density_along_light_ray * cone_spread_multplier;
                    float raininess = saturate(weather_data.g);
                    float overcast = saturate(weather_data.r);
                    float rain_cloud_absorption = max(RAIN_ABSORPTION_MIN, rain_cloud_absorption_scale) * (1.0 + raininess * RAIN_ABSORPTION_RAININESS_SCALE);
                    //光レイ側の Beer-Lambert: 雲の中を通るほど太陽光が減る
                #if 1
                    float beers_law = exp(-optical_thickness * rain_cloud_absorption);
                #else
                    float beers_law = max(exp(-optical_thickness * rain_cloud_absorption), exp(-optical_thickness * 0.25 * rain_cloud_absorption) * 0.35);
                #endif
                    // 曇天/雨天で光量を抑える
                    float direct_occlusion = lerp(1.0, DIRECT_OCCLUSION_MIN, max(overcast, raininess));
                    float ambient_occlusion = lerp(1.0, AMBIENT_OCCLUSION_OVERCAST_MIN, overcast) * lerp(1.0, AMBIENT_OCCLUSION_RAIN_MIN, raininess);

                    float powdered_sugar = enable_powdered_sugar_efffect ? (1.0 - exp(-optical_thickness * POWDERED_SUGAR_THICKNESS_SCALE)) : 1.0;
                    float3 sunset_tint = get_sunset_tint(sun_direction.y);
                    float sun_visibility = get_sun_visibility(sun_direction.y);
                    float direct_visibility = pow(sun_visibility, DIRECT_VISIBILITY_POWER);
                    float ambient_visibility = lerp(NIGHT_AMBIENT_MIN_FACTOR, 1.0, sun_visibility);

                    float3 ambient_light = sky_irradiance
                               * lerp(1.0.xxx, sunset_tint, AMBIENT_SUNSET_BLEND * sun_visibility)
                             * ambient_occlusion
                             * ambient_visibility;

                    float3 direct_light = incoming_sun_light
                         * direct_visibility
                         * beers_law
                        * powdered_sugar
                        * henyey_greenstein_phase
                        * sunset_tint
                        * direct_occlusion;

                    
                    float3 step_light = direct_light + ambient_light;
                    // density_scale が 0 になるゼロ除算を防ぐため max で保護
                    color += transmittence * (step_light / max(density_scale, 1e-5)) * (1.0 - step_transmittance);
                    
                    transmittence *= step_transmittance;
                    
                  
                }
                // ほぼ不透明ならこれ以上足しても見えないので早期終了
                if (transmittence < TRANSMITTANCE_EARLY_OUT)
                {
                    break;
                }
            }
            else
            {
                cloud_test = 0.0;
                zero_density_sample_count = 0;
            }
        }
        else
        {
            float3 weather_data = sample_weather_data(sample_point.xz);
            cloud_test = sample_cloud_density(sample_point, weather_data, density_mip, true);
        }

        prev_t_non_linear = t_non_linear;
    }

    float alpha = 1.0 - transmittence;
    return max(0.0, float4(color, alpha));
}