#include "gltf_model.hlsli"
#include "../Atmosphere.hlsli"
#include "../Sampler.hlsli"

static const float SHADOW_MAP_SIZE = 2048.0f;



struct texture_info
{
    int index; 
    int texcoord; 
};

struct normal_texture_info
{
    int index; 
    int texcoord; 
    float scale;
};

struct occlusion_texture_info
{
    int index; 
    int texcoord; 
    float strength; 
};

struct pbr_metallic_roughness
{
    float4 basecolor_factor; 
    texture_info basecolor_texture;
    float metallic_factor; 
    float roughness_factor; 
    texture_info metallic_roughness_texture;
};

struct material_constants
{
    float3 emissive_factor; 
    int alpha_mode; 
    float alpha_cutoff; 
    int double_sided; 

    pbr_metallic_roughness pbr_metallic_roughness;

    normal_texture_info normal_texture;
    occlusion_texture_info occlusion_texture;
    texture_info emissive_texture;
};

StructuredBuffer<material_constants> materials : register(t11);

#define BASECOLOR_TEXTURE 0
#define METALLIC_ROUGHNESS_TEXTURE 1
#define NORMAL_TEXTURE 2
#define EMISSIVE_TEXTURE 3
#define OCCLUSION_TEXTURE 4

Texture2D<float4> material_textures[5] : register(t2);



Texture2D textureMap : register(t0);
Texture2D color_map : register(t1);

// IBL用テクスチャ
Texture2D lut_charlie : register(t7);
TextureCube diffuse_iem : register(t8);
TextureCube specular_pmrem : register(t9);
Texture2D lut_ggx : register(t10);


Texture2D cascade_shadow_map[4] : register(t12);


float SampleCascadeShadowPCF(int cascadeIndex, float3 shadowCoord, float receiverBias)
{
    float2 texelSize = rcp(float2(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE));
    float pcfRadius = max(cascade_shadow_pcf_radius, 0.5f);

    float occlusion = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2((float) x, (float) y) * texelSize * pcfRadius;
            float weight = (x == 0 && y == 0) ? 4.0f : ((x == 0 || y == 0) ? 2.0f : 1.0f);

            float depth = cascade_shadow_map[cascadeIndex].Sample(sampler_states[BorderWhite], shadowCoord.xy + offset).r;
            float inShadow = ((shadowCoord.z - depth) > receiverBias) ? 1.0f : 0.0f;

            occlusion += inShadow * weight;
            weightSum += weight;
        }
    }

    return occlusion / max(weightSum, 1e-4f);
}


static float D_Charlie(float NdotH, float roughness)
{
  
    float a = max(roughness, 1e-4);
    float invA = rcp(a);
    float sin2 = saturate(1.0 - NdotH * NdotH);
    
    float p = pow(sin2, 0.5 * invA);
    return (2.0 + invA) * p * (1.0 / (2.0 * PI));
}

static float V_Neubelt(float NdotL, float NdotV)
{
    return rcp(4.0 * (NdotL + NdotV - NdotL * NdotV + 1e-4));
}

struct PS_OUTPUT
{
    float4 color : SV_TARGET0;
    float4 normal_roughness : SV_TARGET1;
    float4 material : SV_TARGET2;
};

PS_OUTPUT main(VS_OUT pin, bool is_front_face : SV_IsFrontFace) : SV_TARGET
{
    
    material_constants m = materials[material];

    // ベースカラーとアルファ
    float4 base_color_srgb = m.pbr_metallic_roughness.basecolor_factor;
    if (m.pbr_metallic_roughness.basecolor_texture.index > -1)
    {
        float4 sampled = material_textures[BASECOLOR_TEXTURE].Sample(sampler_states[WrapAnisotropic], pin.texcoord);
        // リニア空間へ
        sampled.rgb = pow(sampled.rgb, GammaFactor);
        base_color_srgb *= sampled;
    }
    
    if (m.alpha_mode == 1) // MASK mode
    {
        if (base_color_srgb.a < m.alpha_cutoff)
        {
            discard;
        }
    }
    
    // 自発光
    float3 emissive = m.emissive_factor;
    if (m.emissive_texture.index > -1)
    {
        emissive *= pow(material_textures[EMISSIVE_TEXTURE].Sample(sampler_states[WrapAnisotropic], pin.texcoord).rgb, 2.2);
    }
    
    //法線と接線ベクトルの準備 
    float3 N_geom = normalize(pin.w_normal.xyz); // ジオメトリ法線
    if (!is_front_face && m.double_sided)
    {
        N_geom = -N_geom;
    }
    float3 N = N_geom; // シェーディング法線

    // 法線マッピング
    if (m.normal_texture.index > -1 && has_tangent > 0)
    {
        float3 T = normalize(pin.w_tangent.xyz);
        float3 B = normalize(cross(N, T));
        if (!is_front_face && m.double_sided)
        {
            T = -T;
            B = -B;
        }

        float3 normal_sample = material_textures[NORMAL_TEXTURE].Sample(sampler_states[WrapLinear], pin.texcoord).xyz;
        float3 normal_tan = (normal_sample * 2.0 - 1.0) * float3(m.normal_texture.scale, m.normal_texture.scale, 1.0);
        
        float3x3 TBN = float3x3(T, B, N);
        N = normalize(mul(normal_tan, TBN));
    }

    // メタリックとラフネス
    float roughness = m.pbr_metallic_roughness.roughness_factor;
    float metallic = m.pbr_metallic_roughness.metallic_factor;
    if (m.pbr_metallic_roughness.metallic_roughness_texture.index > -1)
    {
        float4 mr_sample = material_textures[METALLIC_ROUGHNESS_TEXTURE].Sample(sampler_states[WrapLinear], pin.texcoord);
        roughness *= mr_sample.g; 
        metallic *= mr_sample.b; 
    }
   
    roughness = max(roughness, 0.04);

    // グローバルラフネススケールを適用
    roughness = saturate(roughness * global_roughness_scale);
    roughness = max(roughness, 0.04);
    
 
    //遮蔽 (Occlusion) の適用
    float occlusion = 1.0;
    if (m.occlusion_texture.index > -1)
    {
        // OcclusionはR チャンネル値に格納
        occlusion = material_textures[OCCLUSION_TEXTURE].Sample(sampler_states[WrapLinear], pin.texcoord).r;
        occlusion = lerp(1.0, occlusion, m.occlusion_texture.strength);
    }
    
    float3 V = normalize(camera_position.xyz - pin.w_position.xyz);
    float3 L = normalize(-directional_light_direction.xyz);
    float3 H = normalize(V + L);
    
    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    
    float3 albedo = base_color_srgb;
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    //直接光の計算 
    float3 direct_light_radiance = 0.0f;
    if (NdotL > 0.0f)
    {
        // BRDF計算
        float D = D_GGX(NdotH, roughness);
        float G = G_Smith(NdotV, NdotL, roughness);
        float3 F = F_Schlick(VdotH, F0);
        
        // 鏡面 (Cook-Torrance BRDF)
        float denominator = 4.0 * NdotV * NdotL + 1e-4; // ゼロ除算回避
        float3 specular_brdf = (D * G * F) / denominator;

        // エネルギー保存
        float3 kS = F;
        float3 kD = 1.0 - kS;
        kD *= (1.0 - metallic);

        // 拡散反射項
        float3 diffuse_contrib = (kD * albedo / PI);
        float shadow_factor = 1.0f;
       
        // 太陽が地平線の下にある場合、影を出さない
        float sun_shadow_fade = saturate(sunDirection.y * 10.0); // y=0付近で滑らかにフェード
        
        
        for (int index = 0; index < ShadowBufferSize; ++index)
        {
            float4 wvpPos = pin.cascade_shadow_texcoord[index];

            if (wvpPos.z >= 0 && wvpPos.z <= 1 && wvpPos.x >= 0 && wvpPos.x <= 1 && wvpPos.y >= 0 && wvpPos.y <= 1)
            {
                float depth = cascade_shadow_map[index].Sample(sampler_states[BorderWhite], wvpPos.xy).r;


                float baseBias = cascade_shadow_bias;

                float NdotL_geom = max(dot(N_geom, L), 0.0001f);
                float slope = sqrt(1.0f - NdotL_geom * NdotL_geom) / NdotL_geom;
                float receiverBias = baseBias + baseBias * saturate(slope) * 2.0f;

                float occlusion = SampleCascadeShadowPCF(index, wvpPos.xyz, receiverBias);

                // sun_shadow_fadeで影の効き方を制御
                float shadowStrength = lerp(0.0f, 1.0f - cascade_shadow_attenuation, sun_shadow_fade);
                shadow_factor = 1.0f - occlusion * shadowStrength;

                break;
            }
            
        }
        
        float3 sheen_color = 1.0;
        float sheen_roughness = saturate(roughness);
        float FH = pow(1.0 - VdotH, 5.0);
        float Dsh = D_Charlie(NdotH, sheen_roughness);
        float Vsh = V_Neubelt(NdotL, NdotV);
        float3 sheen_brdf = FH * Dsh * Vsh * sheen_color;

        
        // 直接光 = (拡散 + 鏡面) * ライトカラー * NdotL * 影
        float3 direct_terms = diffuse_contrib + specular_brdf + sheen_brdf * ibl_params.w;

        direct_light_radiance = direct_terms * directional_light_color.rgb * NdotL * shadow_factor;
    }

   
    // フレネル項 (IBL用)
    float3 F_ibl = F_SchlickRoughness(NdotV, F0, roughness);

    // エネルギー保存 (IBL用)
    float3 kS_ibl = F_ibl;
    float3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metallic);

    float day_night_factor = saturate((sunDirection.y - 0.1) / 0.1);
    
    float night_min_light = 0.5;
    float final_factor = max(day_night_factor, night_min_light);
    
    
    
    // 間接拡散光 (Diffuse IBL)
    float3 irradiance = diffuse_iem.SampleLevel(sampler_states[WrapLinear], N, 0).rgb;
    float3 diffuse_ibl = kD_ibl * albedo * irradiance * ibl_params.x * final_factor;
   

    // 間接鏡面光 (Specular IBL)
    const float MAX_REFLECTION_LOD = 7.0;
    float lod = roughness * MAX_REFLECTION_LOD;
    float3 R = reflect(-V, N);
   
    float3 specular_pmrem_sample = specular_pmrem.SampleLevel(sampler_states[WrapLinear], R, lod).rgb;
    // BRDFルックアップテーブル(LUT)から補正値を取得
    float2 brdf_lut_sample = lut_ggx.Sample(sampler_states[ClampLinear], float2(NdotV, roughness)).rg;
    
    float3 specular_ibl = specular_pmrem_sample * (F_ibl * brdf_lut_sample.x + brdf_lut_sample.y) * ibl_params.y * final_factor;
    float sheen_lut = lut_charlie.Sample(sampler_states[ClampLinear], float2(NdotV, roughness)).r;
    float3 sheen_ibl = specular_pmrem_sample * sheen_lut;
    
    
    sheen_ibl *= ibl_params.z;
    
  
    float3 indirect_light_radiance = diffuse_ibl + specular_ibl + sheen_ibl;
    
    
    float3 ambient_contribution = ambient_color.rgb * albedo * final_factor;
    indirect_light_radiance += ambient_contribution;

  
    float3 ambient_radiance = indirect_light_radiance * occlusion;

 
    float3 final_color = direct_light_radiance + ambient_radiance + emissive;

#if 01  
    int debug_shadowmap_index = -1;
#endif  

   
    if (display_cascade_area)
    {
#if 01 
        debug_shadowmap_index = -1;
#endif  
        
       
        for (int index = 0; index < ShadowBufferSize; ++index)
        {
            float4 wvpPos = pin.cascade_shadow_texcoord[index];
            
            if (wvpPos.z >= 0 && wvpPos.z <= 1 && wvpPos.x >= 0 && wvpPos.x <= 1 && wvpPos.y >= 0 && wvpPos.y <= 1)
            {
#if 01  
                debug_shadowmap_index = index;
#endif  
                break;
            }
        }
        
#if 01  
        if (debug_shadowmap_index >= 0)
        {
            float col = rcp((float) (debug_shadowmap_index / 3 + 1));
            float r = debug_shadowmap_index % 3 == 0;
            float g = debug_shadowmap_index % 3 == 1;
            float b = debug_shadowmap_index % 3 == 2;
            final_color = float3(r, g, b) * col;
        }
        else
        {
            final_color = 0;
        }
#endif  //  defined(_DEBUG)
    }

    
    
    
    PS_OUTPUT o;
    o.color = float4(final_color, base_color_srgb.a);

    
    float3 n_encoded = normalize(N);
    o.normal_roughness = float4(n_encoded, saturate(roughness));
    
    
    o.material = float4(saturate(metallic), saturate(occlusion), 0.0f, 1.0f);
    return o;
    
  
   
}