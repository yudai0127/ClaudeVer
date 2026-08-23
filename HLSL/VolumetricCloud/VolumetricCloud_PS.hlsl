#include "VolumetricCloud.hlsli"

static const float NDC_SCALE = 2.0;
static const float NDC_BIAS = 1.0;
static const float RAYMARCH_DIRECTION_Y_THRESHOLD = 0.0;

static const float AUTO_ZENITH_STEP_SCALE = 0.52;
static const float AUTO_HORIZON_STEP_SCALE = 1.0;
static const float MIN_RAY_MARCH_STEPS = 64.0;
static const float MAX_RAY_MARCH_STEPS = 128.0;
static const float TARGET_RAY_STEP_LENGTH = 250.0;




static const float MAX_CLOUD_VIEW_DISTANCE = 35000.0;
static const float CLOUD_DISTANCE_FADE_START = 30000.0;

static const float HORIZON_VISIBILITY_START_Y = 0.003;
static const float HORIZON_VISIBILITY_END_Y = 0.055;




static const float CLOUD_RAY_MIDPOINT = 0.5;

float4 main(VS_OUT pin) : SV_TARGET
{
    float4 ndc = float4(
        NDC_SCALE * pin.texcoord.x - NDC_BIAS,
        NDC_BIAS - NDC_SCALE * pin.texcoord.y,
        0.0,
        1.0
    );


    float4 pos = mul(ndc, inverse_view_projection);
    pos /= pos.w;

   
    float3 ray_dir = normalize(pos.xyz - camera_position.xyz);
    float3 background = skybox.SampleLevel(sampler_states[ClampLinear], ray_dir, 0).rgb;
    float3 color = background;

    float cloud_planet_radius = get_cloud_planet_radius();
    float3 eye_pos = float3(0.0, cloud_planet_radius, 0.0) + camera_position.xyz;
    float eye_radius = length(eye_pos);

    float cloud_bottom_radius = cloud_planet_radius + cloud_altitudes_min_max.x;
    float cloud_top_radius = cloud_planet_radius + cloud_altitudes_min_max.y;
    bool inside_cloud_layer = (eye_radius > cloud_bottom_radius) && (eye_radius < cloud_top_radius);

    bool below_cloud_layer = eye_radius <= cloud_bottom_radius;
    bool above_cloud_layer = eye_radius >= cloud_top_radius;
    bool looking_toward_planet = dot(eye_pos, ray_dir) < 0.0;
    bool can_hit_cloud_layer = inside_cloud_layer
                            || (below_cloud_layer && ray_dir.y > RAYMARCH_DIRECTION_Y_THRESHOLD)
                            || (above_cloud_layer && looking_toward_planet);

    if (can_hit_cloud_layer)
    {
        float t_outer_near = 0.0;
        float t_outer_far = 0.0;
        if (!intersect_sphere_range(eye_pos, ray_dir, cloud_top_radius, t_outer_near, t_outer_far) || t_outer_far <= 0.0)
        {
            return float4(background, 1.0);
        }

        float t_inner_near = 0.0;
        float t_inner_far = 0.0;
        bool hit_inner = intersect_sphere_range(eye_pos, ray_dir, cloud_bottom_radius, t_inner_near, t_inner_far);

        float start_t = 0.0;
        float end_t = t_outer_far;

        if (eye_radius >= cloud_top_radius)
        {
            start_t = t_outer_near;
            end_t = hit_inner ? min(t_inner_near, t_outer_far) : t_outer_far;
        }
        else if (eye_radius <= cloud_bottom_radius)
        {
            start_t = hit_inner ? t_inner_far : t_outer_near;
            end_t = t_outer_far;
        }
        else
        {
            start_t = 0.0;
            end_t = t_outer_far;
        }

        start_t = max(start_t, 0.0);
        end_t = min(max(end_t, 0.0), MAX_CLOUD_VIEW_DISTANCE);

        if (start_t >= MAX_CLOUD_VIEW_DISTANCE || end_t <= start_t)
        {
            return float4(background, 1.0);
        }

        float3 ray_origin = eye_pos + ray_dir * start_t;
        float3 ray_endpoint = eye_pos + ray_dir * end_t;

        float shell_dist = length(ray_endpoint - ray_origin);

        float steps = ray_marching_steps;
        if (auto_ray_marching_steps)
        {
            float horizon_weight = 1.0 - saturate(ray_dir.y);
            steps = lerp(ray_marching_steps * AUTO_ZENITH_STEP_SCALE,
                         ray_marching_steps * AUTO_HORIZON_STEP_SCALE,
                         horizon_weight);
        }




        float distance_limited_steps = max(MIN_RAY_MARCH_STEPS,
                                           shell_dist / TARGET_RAY_STEP_LENGTH);
        steps = min(steps, distance_limited_steps);

        // 距離からステップ数を強制すると、雲層の単位スケールではほぼ常に

        steps = clamp(steps, MIN_RAY_MARCH_STEPS, MAX_RAY_MARCH_STEPS);

        float3 ray_step = ray_dir * shell_dist / steps;
        float ray_jitter = CLOUD_RAY_MIDPOINT;

        float4 volume = ray_march(ray_origin,
                                  ray_step,
                                  int(steps),
                                  start_t,
                                  CLOUD_DISTANCE_FADE_START,
                                  MAX_CLOUD_VIEW_DISTANCE,
                                  ray_jitter);




        float layer_thickness = max(cloud_altitudes_min_max.y
                                  - cloud_altitudes_min_max.x,
                                    1.0);
        float depth_scale = layer_thickness
                          * max(cloud_density_long_distance_scale, 1.0);
        float atmosphere_occlusion = 1.0 - exp(-start_t / depth_scale);
        volume.rgb = lerp(volume.rgb,
                          background * volume.a,
                          saturate(atmosphere_occlusion * 0.35));





        // 小さな雲片が丸い光点・色むらとして浮き出る。元の空色で合成する。
        float3 blended = background * (1.0 - volume.a) + volume.xyz;
        color = blended;
    }

    return float4(color, 1);
}
