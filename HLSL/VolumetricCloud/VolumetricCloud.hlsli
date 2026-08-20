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
static const float CURL_DISTORTION_BASE = 520.0;
static const float CURL_DISTORTION_TOP = 220.0;

static const int CONE_SAMPLE_COUNT = 3;
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

static const float RAY_MARCH_MIDPOINT = 0.5;
// 位相関数（二重ローブ Henyey-Greenstein）
static const float PHASE_G_FORWARD = 0.65;         // 前方散乱ローブ（太陽側の輝き）
static const float PHASE_G_BACKWARD = -0.25;       // 後方散乱ローブ（雲縁のシルバーライニング）
static const float PHASE_LOBE_BLEND = 0.4;         // 後方ローブの混合比
static const float ISOTROPIC_PHASE = 0.0795774715; // 1 / (4 * PI)
static const float PHASE_MAX_GAIN = 2.2;           // 等方散乱に対する最大ゲイン
static const float PHASE_MIN_GAIN = 0.85;          // 等方散乱に対する最小ゲイン
static const float CONE_SPREAD_DIVISOR = 36.0;
static const float SEGMENT_STEP_EPSILON = 1e-5;
static const int ZERO_DENSITY_RESET_COUNT = 6;
static const float TRANSMITTANCE_EARLY_OUT = 0.01;

static const float RAIN_ABSORPTION_MIN = 0.10;
static const float RAIN_ABSORPTION_RAININESS_SCALE = 1.35;
static const float SUNNY_CLOUD_ABSORPTION = 0.16;
static const float DIRECT_OCCLUSION_MIN = 0.42;
static const float AMBIENT_OCCLUSION_STORM_MIN = 0.62;
static const float POWDERED_SUGAR_THICKNESS_SCALE = 1.60;
static const float AMBIENT_SUNSET_BLEND = 0.35;
static const float CLOUD_DIRECT_LUMINANCE_LIMIT = 2.2;
static const float CLOUD_LUMINANCE_PER_OPACITY_LIMIT = 2.4;
static const float3 LUMINANCE_WEIGHTS = float3(0.2126, 0.7152, 0.0722);

// Stable, low-energy multiple scattering. The previous large white/core fills
// erased the optical-depth variation and made the clouds look like solid clay.
static const float MULTI_SCATTER_EXTINCTION_SCALE = 0.28;
static const float MULTI_SCATTER_CONTRIBUTION = 0.24;
static const float CLOUD_AMBIENT_NEUTRALITY = 0.72;
static const float CLOUD_BASE_AMBIENT_MIN = 0.58;
static const float CLOUD_AMBIENT_SCALE = 0.68;
static const float CLOUD_OPTICAL_AMBIENT_MIN = 0.42;

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
    float2 normalized = v * rsqrt(max(len2, 1e-8));
    return lerp(float2(1.0, 0.0), normalized, step(1e-8, len2));
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
    float low_frequency_fbm = dot(low_frequency_noises.gba,
                                  float3(0.625, 0.25, 0.125));

    // The weather texture stores a continuous local coverage value. Keeping it
    // continuous is essential: a binary mask forces every visible cell to the
    // same maximum density and produces giant vertical columns.
    float cloud_coverage = saturate(weather_data.r * cloud_coverage_scale);
    float cloud_type = saturate(weather_data.b * cloud_type_scale);

    // Horizon-style base signal: Perlin provides connected masses while the
    // packed Worley octaves erode them into broad, rounded billows.
    float shape_signal = remap(low_frequency_noises.r,
                               -(1.0 - low_frequency_fbm),
                               1.0,
                               0.0,
                               1.0);
    // The three blended height profiles are the only vertical shape control.
    // Removing the old local-top/tower-taper path prevents hanging pillars.
    float density_height_gradient = get_density_height_gradient(height_fraction,
                                                                 cloud_type);
    float height_shaped_density = shape_signal * density_height_gradient;

    // Coverage shifts the density threshold instead of multiplying a binary
    // placement mask. This is the key relationship used by the HZD method.
    float final_cloud = remap(height_shaped_density,
                              1.0 - cloud_coverage,
                              1.0,
                              0.0,
                              1.0);
    final_cloud *= cloud_coverage;

    if (!cheap_sample && final_cloud > 0.0)
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

        // Decode the 2D curl texture as a signed vector and use it to distort
        // only the detail noise. This adds turbulence without breaking the
        // readable low-frequency cloud silhouette.
        float2 curl_uv = detail_point.xz * CURL_NOISE_UV_SCALE;
        float2 curl_vector = curl_noise_texture.SampleLevel(
            sampler_states[LINEAR_MIRROR], curl_uv, 0).xy * 2.0 - 1.0;
        float curl_strength = lerp(CURL_DISTORTION_BASE,
                                   CURL_DISTORTION_TOP,
                                   smoothstep(0.0, 0.85, height_fraction));
        detail_point.xz += curl_vector * curl_strength;

        float3 high_frequency_noises = sample_high_frequency_noises(detail_point, mip_level);
        float high_frequency_fbm = dot(high_frequency_noises,
                                       float3(0.625, 0.25, 0.125));

        // Invert detail near the base for wispy rising edges, then use it as a
        // remap threshold. This erodes the perimeter without exposing Worley
        // rings as circular craters across the whole cloud body.
        float detail_height_blend = smoothstep(0.10, 0.32, height_fraction);
        float erosion_noise = lerp(high_frequency_fbm,
                                   1.0 - high_frequency_fbm,
                                   detail_height_blend);
        float erosion_threshold = erosion_noise
                                * lerp(0.035, 0.11,
                                       smoothstep(0.08, 0.90, height_fraction));
        final_cloud = remap(final_cloud,
                            erosion_threshold,
                            1.0,
                            0.0,
                            1.0);
    }

    final_cloud = saturate(final_cloud);
    float coherent_cloud = smoothstep(0.003, 0.045, final_cloud);
    return final_cloud * coherent_cloud;
}


// 雲の明るさ全体スケール
static const float CLOUD_LIGHT_SCALE = 0.90;


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
    // g が 1 に近いと分母が 0 に落ちて発散するのでクランプする
    float denom = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-4);
    return (1.0 - g * g) / (pow(denom, 1.50) * 4 * PI);
}


// 二重ローブ Henyey-Greenstein
// 実際の雲は水滴が大きいため、強い前方散乱（太陽側の輝き）と
// 後方散乱（雲縁のシルバーライニング）の両方を持つ。
// 単一ローブでは g をどう選んでもどちらか片方しか再現できない。
// どちらのローブも球面上で 1 に正規化されているので、混合しても総エネルギーは保存される
float dual_lobe_henyey_greenstein(float cos_theta, float g_forward, float g_backward, float blend)
{
    return lerp(henyey_greenstein(cos_theta, g_forward),
                henyey_greenstein(cos_theta, g_backward),
                blend);
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
    // 全ての経路で有効な値を返し、水平線付近の視線で未定義値が光に化けるのを防ぐ。
    t_near = 0.0;
    t_far = 0.0;

    float a = dot(dir, dir);
    if (a <= 1e-8 || isnan(a) || isinf(a))
    {
        return false;
    }

    float b = 2.0 * dot(dir, pos);
    float c = dot(pos, pos) - (r * r);
    float disc = (b * b) - 4.0 * a * c;

    if (disc < 0.0 || isnan(disc) || isinf(disc))
    {
        return false;
    }

    float d = sqrt(disc);
    float inv_2a = 0.5 / a;
    t_near = (-b - d) * inv_2a;
    t_far = (-b + d) * inv_2a;

    if (isnan(t_near) || isnan(t_far) || isinf(t_near) || isinf(t_far))
    {
        t_near = 0.0;
        t_far = 0.0;
        return false;
    }

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

    // コーンの全長は雲層の厚みの 1/9 程度しかなく、天候マップは数十kmスケールでしか
    // 変化しないため、コーン内では一定とみなして起点で1回だけサンプルする
    float3 weather_data = sample_weather_data(ray_origin.xz);

    [unroll]
    for (int i = 0; i < CONE_SAMPLE_COUNT; i++)
    {
        float t = (i + 1) * stepLen;

        // 距離が伸びるほどコーンが太くなる
        float3 lateral = noise_kernel[i] * (t * CONE_LATERAL_SCALE);
        float3 sample_point = ray_origin + ray_direction * t + lateral;

        // ここは自己影の近似でしかないので、高周波ノイズ(curl + Worley)は
        // 影の形に一番効く最寄りのサンプルだけで評価し、残りは安価なサンプルで済ませる
        bool cheap_sample = (i > 0);
        density_along_cone += sample_cloud_density(sample_point, weather_data, float(i), cheap_sample);
    }

    return density_along_cone;
}


// 遠距離1点の密度サンプル
float sample_cloud_density_long_distance(float3 ray_origin, float3 ray_direction, float cone_spread_multplier)
{
    const float long_distance = cloud_density_long_distance_scale * cone_spread_multplier;

    float3 sample_point = ray_origin + ray_direction * long_distance;
    float3 weather_data = sample_weather_data(sample_point.xz);

    // ミップ5で参照するため高周波ノイズはほぼ潰れる。安価なサンプルで十分
    return sample_cloud_density(sample_point, weather_data, LONG_DISTANCE_DENSITY_MIP, true);
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

    const float3 warm_tint = float3(1.0, 0.68, 0.48);
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

    // ピクセルごとのハッシュジッターは、薄い雲を拾う画素と外す画素を作り、
    // 夕方の強い前方散乱で光点に見える。安定した区間中点サンプリングを使う。
    float jitter = RAY_MARCH_MIDPOINT;
    float3 sample_point = ray_origin;

    // 太陽方向と位相関数(散乱の向き)を準備
    float3 sun_direction = normalize(sunDirection.xyz);
    float3 view_dir = normalize(ray_step);
    // Incoming sunlight travels opposite to sun_direction and the scattered
    // ray travels toward the camera. Their angle reduces to dot(sun, view).
    float cos_theta = dot(sun_direction, view_dir);

    // 位相関数(Phase Function)の計算
    // 以前は g=0.1 のほぼ等方散乱だったため、どの向きから見ても明るさが変わらず
    // 雲がのっぺりして見えていた。二重ローブにして方向性を持たせる
    float henyey_greenstein_phase = dual_lobe_henyey_greenstein(
        cos_theta, PHASE_G_FORWARD, PHASE_G_BACKWARD, PHASE_LOBE_BLEND);

    // 太陽の真正面ではローブが鋭く尖るのでフレアが暴れないよう上限を設ける。
    // 逆に横方向は暗くなりすぎるため下限も設け、従来の明るさから大きく外れないようにする
    henyey_greenstein_phase = clamp(henyey_greenstein_phase,
                                    ISOTROPIC_PHASE * PHASE_MIN_GAIN,
                                    ISOTROPIC_PHASE * PHASE_MAX_GAIN);

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
                    float local_coverage = saturate(weather_data.r);
                    float storminess = max(raininess,
                                           smoothstep(0.58, 0.82, local_coverage) * 0.65);
                    // The GUI rain absorption value must not darken fair-weather
                    // cumulus. Blend from a low sunny extinction into the much
                    // stronger rain-cloud extinction only as precipitation rises.
                    float rain_extinction = max(RAIN_ABSORPTION_MIN, rain_cloud_absorption_scale);
                    float cloud_absorption = lerp(SUNNY_CLOUD_ABSORPTION,
                                                  rain_extinction,
                                                  raininess)
                                           * (1.0 + raininess * RAIN_ABSORPTION_RAININESS_SCALE);
                    // Beer-Lambert primary transmission plus one low-energy
                    // multiple-scattering octave keeps dense cores readable.
                    float primary_scattering = exp(-optical_thickness * cloud_absorption);
                    float multiple_scattering = exp(-optical_thickness * cloud_absorption
                                                   * MULTI_SCATTER_EXTINCTION_SCALE)
                                              * MULTI_SCATTER_CONTRIBUTION;
                    float beers_law = saturate(primary_scattering + multiple_scattering);

                    // Coverage describes where a cloud exists, not whether it is
                    // automatically a rain cloud. Only dense overcast cells and
                    // actual rain strongly suppress direct sunlight.
                    float direct_occlusion = lerp(1.0, DIRECT_OCCLUSION_MIN, storminess);
                    float ambient_occlusion = lerp(1.0, AMBIENT_OCCLUSION_STORM_MIN, storminess);

                    // Beer-Powder is strongest toward the sun, producing a soft
                    // silver lining instead of a uniform white outline.
                    float powder_response = 1.0 - exp(-optical_thickness * POWDERED_SUGAR_THICKNESS_SCALE);
                    float powder_view = smoothstep(0.20, 0.88, cos_theta);
                    float powdered_sugar = enable_powdered_sugar_efffect
                        ? 1.0 + powder_response * powder_view * 0.28
                        : 1.0;
                    float3 sunset_tint = get_sunset_tint(sun_direction.y);
                    float sun_visibility = get_sun_visibility(sun_direction.y);
                    float direct_visibility = pow(sun_visibility, DIRECT_VISIBILITY_POWER);
                    float ambient_visibility = lerp(NIGHT_AMBIENT_MIN_FACTOR, 1.0, sun_visibility);

                    float sky_luminance = dot(sky_irradiance, LUMINANCE_WEIGHTS);
                    float3 neutral_sky_irradiance = lerp(sky_irradiance,
                                                         sky_luminance.xxx,
                                                         CLOUD_AMBIENT_NEUTRALITY);
                    float height_fraction = get_height_fraction_for_point_radius(current_height);
                    float base_light = lerp(CLOUD_BASE_AMBIENT_MIN, 1.0,
                                            smoothstep(0.02, 0.72, height_fraction));

                    // Optical depth now creates the broad shaded side and the
                    // darker condensation base; no uniform white core fill.
                    float optical_ambient = lerp(1.0,
                                                 CLOUD_OPTICAL_AMBIENT_MIN,
                                                 1.0 - exp(-optical_thickness * 0.10));
                    float density_ambient = lerp(1.0, 0.78,
                                                 smoothstep(0.18, 0.72, sampled_density));

                    float3 ambient_light = neutral_sky_irradiance
                               * lerp(1.0.xxx, sunset_tint, AMBIENT_SUNSET_BLEND * sun_visibility)
                              * ambient_occlusion
                              * base_light
                              * optical_ambient
                              * density_ambient
                              * CLOUD_AMBIENT_SCALE
                              * ambient_visibility;

                    float vertical_direct = lerp(0.76, 1.04,
                                                 smoothstep(0.04, 0.78, height_fraction));
                    float3 direct_light = incoming_sun_light
                         * direct_visibility
                         * beers_law
                        * powdered_sugar
                        * henyey_greenstein_phase
                        * sunset_tint
                        * direct_occlusion
                        * vertical_direct;

                    // 一つのレイサンプルだけが極端に明るくなるファイアフライを抑制。
                    // 色相は保ったまま輝度だけを制限する。
                    float direct_luminance = dot(direct_light, LUMINANCE_WEIGHTS);
                    direct_light *= min(1.0, CLOUD_DIRECT_LUMINANCE_LIMIT / max(direct_luminance, 1e-5));

                    
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

    // レイ全体でも透過率が非常に低い雲片は、色と透明度を一緒にフェードする。
    float opacity_gate = smoothstep(0.04, 0.16, alpha);
    color *= opacity_gate;
    alpha *= opacity_gate;

    // 稀薄な雲の色が透明度に対して明るすぎると、Bloomで点状に広がる。
    // プリマルチプライドカラーのエネルギーを不透明度に連動させる。
    float integrated_luminance = dot(color, LUMINANCE_WEIGHTS);
    float integrated_limit = max(alpha * CLOUD_LUMINANCE_PER_OPACITY_LIMIT, 1e-5);
    color *= min(1.0, integrated_limit / max(integrated_luminance, 1e-5));

    return max(0.0, float4(color, alpha));
}
