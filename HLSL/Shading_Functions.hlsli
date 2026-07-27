static const float PI = 3.14159265f;

// χ+(x)関数の実装：xが0より大きい場合は1、それ以外は0
float Chi(float x)
{
    return x > 0.0 ? 1.0 : 0.0;
}



//	可視性関数
float CalcVisibilityGGX(float3 N, float3 H, float3 V, float3 L, float alpha)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float HdotL = dot(H, L);
    float HdotV = dot(H, V);
    
    // χ+(H・L)とχ+(H・V)の計算
    float chiHL = Chi(HdotL);
    float chiHV = Chi(HdotV);
    
    // 分母の計算 - 画像の式と同じ
    float denomL = NdotL + sqrt(alpha * alpha + (1.0 - alpha * alpha) * (NdotL * NdotL));
    float denomV = NdotV + sqrt(alpha * alpha + (1.0 - alpha * alpha) * (NdotV * NdotV));
    
    // 可視性関数の計算
    return (2.0 * NdotL * chiHL) / denomL * (2.0 * NdotV * chiHV) / denomV;
}



//	ランバート拡散反射計算関数
float3 CalcLambert(float3 N, float3 L, float3 C, float3 K)
{
    //float power = max(0, dot(N, -L));
    float power = saturate(dot(N, -L));
    return C * power * K;
}


//	フォンの鏡面反射計算関数
float CalcPhongSpecular(float3 N, float3 L, float3 E, float3 C, float3 K)
{
    float3 R = reflect(L, N);
    float power = max(dot(-E, R), 0);
    power = pow(power, 128);
    return C * power * K;
}

//	ハーフランバート拡散反射計算関数
float3 CalcHalfLambert(float3 N, float3 L, float3 C, float3 K)
{
    float D = saturate(dot(N, -L) * 0.5f + 0.5f);
    return C * D * K;
}

// リムライト
float3 CalcRimLight(float3 N, float3 E, float3 L, float3 C, float RimPower = 1.0f)
{
    float rim = 1.0f - saturate(dot(N, -E));
    return C * pow(rim, RimPower) * saturate(dot(L, -E));
}

// ランプシェーディング
float3 CalcRampShading(Texture2D tex, SamplerState samp, float3 N, float3 L, float3 C, float3 K)
{
    float D = saturate(dot(N, -L) * 0.5f + 0.5f);
    float3 Ramp = tex.Sample(samp, float2(D, 0.5f)).r;
    return C * Ramp * K.rgb;
}


// 球体環境マッピング
float3 CalcSphereEnvironment(Texture2D tex, SamplerState samp, in float3 color, float3 N, float3 E, float value)
{
    float3 R = reflect(E, N);
    float2 texcoord = R.xy * 0.5f + 0.5f;
    return lerp(color.rgb, tex.Sample(samp, texcoord).rgb, value);
}


// 半球ライティング
float3 CalcHemiSphereLight(float3 normal, float3 up, float3 sky_color, float3 ground_color, float4
hemisphere_weight)
{
    float factor = dot(normal, up) * 0.5f + 0.5f;
    return lerp(ground_color, sky_color, factor) * hemisphere_weight.x;
}


//	フォグ
float4 CalcFog(in float4 color, float4 fog_color, float2 fog_range, float eye_length)
{
    float fogAlpha = saturate((eye_length - fog_range.x) / (fog_range.y - fog_range.x));
    return lerp(color, fog_color, fogAlpha);
}




// フレネル反射の計算 (Schlick近似)
float3 CalcFresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// 正規化されたGGX分布関数 (法線分布関数)
float CalcDistributionGGX(float3 N, float3 H, float roughness)
{
    
    
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / max(denom, 0.0000001); // 0除算防止
}

// 幾何減衰の計算 (Smith GGX)
float CalcGeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / max(denom, 0.0000001); // 0除算防止
}

// Smith法によるGeometry関数
float CalcGeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = CalcGeometrySchlickGGX(NdotV, roughness);
    float ggx2 = CalcGeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

//	粗さを考慮したフレネル項の近似式
float3 CalcFresnelRoughness(float3 f0, float NdotV, float roughness)
{
    return f0 + (max((float3) (1.0f - roughness), f0) - f0) * pow(saturate(1.0f - NdotV), 5.0f);
}

//	キューブマップから照度を取得
float4 SampleDiffuseIEM(float3 v, TextureCube diffuse_iem_cube_map, SamplerState state)
{
    return diffuse_iem_cube_map.Sample(state, v);
}

// PBRシェーディング計算メイン関数
float3 CalcPBR(float3 albedo, float3 N, float3 V, float3 L, float3 lightColor, float metallic, float roughness, float3 ambient)
{
   
    
    // 各種ベクトル
    float3 H = normalize(V + -L); // ハーフベクトル
    
    // 光の入射角
    float NdotL = max(dot(N, -L), 0.0);
    
    // ベースとなる反射率（非金属の場合は0.04、金属の場合はalbedo）
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo, metallic);
    
    // Cook-Torrance BRDF
    float NDF = CalcDistributionGGX(N, H, roughness);
    float G = CalcGeometrySmith(N, V, -L, roughness);
    float3 F = CalcFresnelSchlick(max(dot(H, V), 0.0), F0);
    
    // 鏡面反射成分と拡散反射成分を計算
    float3 kS = F; // 鏡面反射率
    float3 kD = float3(1.0, 1.0, 1.0) - kS; // 拡散反射率
    kD *= 1.0 - metallic; // 金属は拡散反射しない
    
    // スペキュラBRDFの計算
    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(NdotL, 0.0) + 0.0001;
    float3 specular = numerator / denominator;
    
    // 出力カラーの計算
    float3 radiance = lightColor * NdotL;
    float3 diffuse = kD * albedo / PI;
    
    // 直接光の計算結果
    float3 directLight = (diffuse + specular) * radiance;
    
    // 環境光（間接光）を加算
    float3 indirectLight = ambient * albedo;
    
    // 最終的なカラー
    return directLight + indirectLight;
}

//	ルックアップテーブルからGGX項を取得
float4 SampleLutGGX(float2 brdf_sample_point, Texture2D lut_ggx_map, SamplerState state)
{
    return lut_ggx_map.Sample(state, brdf_sample_point);
}

//	キューブマップから放射輝度を取得
float4 SampleSpecularPMREM(float3 v, float roughness, TextureCube specular_pmrem_cube_map, SamplerState state)
{
    //  ミップマップによって粗さを表現するため、段階を算出
    uint width, height, mip_maps;
    specular_pmrem_cube_map.GetDimensions(0, width, height, mip_maps);
    float lod = roughness * float(mip_maps - 1);
    return specular_pmrem_cube_map.SampleLevel(state, v, lod);
}

//	拡散反射IBL
float3 DiffuseIBL(float3 normal, float3 eye_vector, float roughness, float3 diffuse_reflectance, float3 f0, TextureCube diffuse_iem_cube_map, SamplerState state)
{
    float3 N = normal;
    float3 V = -eye_vector;

    //  間接拡散反射光の反射率計算
    float NdotV = max(0.0001f, dot(N, V));
    float3 kD = 1.0f - CalcFresnelRoughness(f0, NdotV, roughness);

    float3 irradiance = SampleDiffuseIEM(normal, diffuse_iem_cube_map, state).rgb;
    return diffuse_reflectance * irradiance * kD;
}

//	鏡面反射IBL
float3 SpecularIBL(float3 normal, float3 eye_vector, float roughness, float3 f0, Texture2D lut_ggx_map, TextureCube specular_pmrem_cube_map, SamplerState state)
{
    float3 N = normal;
    float3 V = -eye_vector;

    float NdotV = max(0.0001f, dot(N, V));
    float3 R = normalize(reflect(-V, N));
    float3 specular_light = SampleSpecularPMREM(R, roughness, specular_pmrem_cube_map, state).rgb;

    float2 brdf_sample_point = saturate(float2(NdotV, roughness));
    float2 env_brdf = SampleLutGGX(brdf_sample_point, lut_ggx_map, state).rg;
    return specular_light * (f0 * env_brdf.x + env_brdf.y);
}

//	フレネル項
float3 CalcFresnel(float3 F0, float VdotH)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - VdotH, 0.0f, 1.0f), 5.0f);
}




//	幾何減衰項の算出
float CalcGeometryFunction(float NdotL, float NdotV, float roughness)
{
    float r = roughness * 0.5f;
    float shadowing = NdotL / (NdotL * (1.0 - r) + r);
    float masking = NdotV / (NdotV * (1.0 - r) + r);
    return shadowing * masking;
}


//	拡散反射BRDF
float3 DiffuseBRDF(float VdotH, float3 fresnelF0, float3 diffuseReflectance)
{
    
    return (1.0f - CalcFresnel(fresnelF0, VdotH)) * (diffuseReflectance / PI);
}

//	法線分布関数
float CalcNormalDistributionFunction(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float b = (NdotH * NdotH) * (a - 1.0f) + 1.0f;
    return a / (PI * b * b);
}


//	鏡面反射BRDF
float3 SpecularBRDF(float NdotV, float NdotL, float NdotH, float VdotH, float3 fresnelF0, float roughness)
{
	//	D項(法線分布)
    float D = CalcNormalDistributionFunction(NdotH, roughness);
	//	G項(幾何減衰項)
    float G = CalcGeometryFunction(NdotL, NdotV, roughness);
	//	F項(フレネル反射)
    float3 F = CalcFresnel(fresnelF0, VdotH);

    return D * G * F / (NdotL * NdotV * 4.0f);
}






//	直接光の物理ベースライティング
void DirectBRDF(float3 diffuse_reflectance,
                float3 F0,
                float3 normal,
                float3 eye_vector,
                float3 light_vector,
                float3 light_color,
                float roughness,
                out float3 out_diffuse,
                out float3 out_specular)
{
    float3 N = normal;
    float3 L = -light_vector;
    float3 V = -eye_vector;
    float3 H = normalize(L + V);

    float NdotV = max(0.0001f, dot(N, V));
    float NdotL = max(0.0001f, dot(N, L));
    float NdotH = max(0.0001f, dot(N, H));
    float VdotH = max(0.0001f, dot(V, H));

    float3 irradiance = light_color * NdotL;
    
    // 拡散反射BRDF
    out_diffuse = DiffuseBRDF(VdotH, F0, diffuse_reflectance) * irradiance;
    
    // 鏡面反射BRDF
    out_specular = SpecularBRDF(NdotV, NdotL, NdotH, VdotH, F0, roughness) * irradiance;
}

float3 CalcFresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 one = float3(1.0, 1.0, 1.0);
    float3 zero = float3(0.0, 0.0, 0.0);
    return F0 + (max(one - F0, zero)) * pow(1.0 - cosTheta, 5.0) * (1.0 - roughness);
}


//	クリアコートBRDFの計算
float3 CalcClearCoatBRDF(float3 N, float3 V, float3 L, float3 H, float roughness)
{
    
    static const float3 F0 = float3(0.04, 0.04, 0.04); // クリアコートのF0は固定値（非金属）
    
    float NdotH = max(dot(N, H), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    
    // D項 (法線分布関数)
    float D = CalcDistributionGGX(N, H, roughness);
    
    // G項 (幾何減衰項)
    float G = CalcGeometrySmith(N, V, L, roughness);
    
    // F項 (フレネル項) - クリアコートは非金属なので固定のF0を使用
    float3 F = CalcFresnelSchlick(VdotH, F0);
    
    // スペキュラBRDF
    float3 specular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.001);
    
    return specular * NdotL;
}


//	スペキュラBRDFの計算
float3 CalcImageBasedSpecularBRDF(float3 N, float3 H, float3 V, float3 L, float roughness, float3 F0)
{
   
    
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    
    
    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    
   
    float chiNH = (NdotH > 0.0) ? 1.0 : 0.0;
    float chiHL = (dot(H, L) > 0.0) ? 1.0 : 0.0;
    float chiHV = (HdotV > 0.0) ? 1.0 : 0.0;
    
    
    float D = (alpha2 * chiNH) / (PI * pow((NdotH * NdotH) * (alpha2 - 1.0) + 1.0, 2.0));
    
    
    float denomL = NdotL + sqrt(alpha2 + (1.0 - alpha2) * (NdotL * NdotL));
    float denomV = NdotV + sqrt(alpha2 + (1.0 - alpha2) * (NdotV * NdotV));
    float G = (2.0 * NdotL * chiHL) / denomL * (2.0 * NdotV * chiHV) / denomV;
    
    
    float3 F = F0 + (1.0 - F0) * pow(1.0 - HdotV, 5.0);
    
     float3 specular = (D * G * F) / max(4.0 * NdotL * NdotV, 0.001); // 0除算防止

    return specular;
}

//	IBL鏡面反射計算
float3 ImageBasedSpecularIBL(float3 normal, float3 eye_vector, float roughness, float3 F0,
                             Texture2D lut_ggx_map, TextureCube specular_pmrem_cube_map,
                             SamplerState state)
{
    float3 N = normal;
    float3 V = -eye_vector;
    float NdotV = max(0.0001f, dot(N, V));
    
    // 反射ベクトル
    float3 R = normalize(reflect(-V, N));
    
    // ミップマップを使用して異なる粗さレベルに対応した事前計算済み環境マップからサンプリング
    uint width, height, mip_maps;
    specular_pmrem_cube_map.GetDimensions(0, width, height, mip_maps);
    float lod = roughness * float(mip_maps - 1);
    float3 specular_light = specular_pmrem_cube_map.SampleLevel(state, R, lod).rgb;
    
    // BRDFの積分項を参照（LUT）
    float2 brdf_sample_point = saturate(float2(NdotV, roughness));
    float2 env_brdf = lut_ggx_map.Sample(state, brdf_sample_point).rg;
    
    
    return specular_light * (F0 * env_brdf.x + env_brdf.y);
}

float3 ACESFilmicToneMapping(float3 color)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}


float SchlickFresnel(float cosTheta)
{
    return pow(clamp((1.0 - cosTheta), 0.0, 1.0), 5.0);
}

float D_GTR1(float NdotH, float a)
{
    float a2 = a * a;
    float tmp = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * tmp);
}

float D_GTR2aniso(float NdotH, float HdotX, float HdotY, float ax, float ay)
{
    float tmp = (HdotX * HdotX) / (ax * ax) + (HdotY * HdotY) / (ay * ay) + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * tmp * tmp);
}

float G_GGX(float NdotV, float a)
{
    float a2 = a * a;
    float down = NdotV + sqrt(a2 + NdotV * NdotV - a2 * NdotV * NdotV);
    return 1.0 / down;
}

float G_GGXaniso(float NdotV, float VdotX, float VdotY, float ax, float ay)
{
    float tmp = VdotX * VdotX * ax * ax + VdotY * VdotY * ay * ay + NdotV * NdotV;
    float down = NdotV + sqrt(tmp);
    return 1.0 / down;
}

float3 DisneyBRDF(float3 L, float3 V, float3 N, float3 H, float3 X, float3 Y, float3 baseColor,
                 float subsurface, float metallic, float specular, float specularTint,
                 float roughness, float anisotropic, float sheen, float sheenTint,
                 float clearcoat, float clearcoatGloss)
{
    float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    if (NdotL < 0.0 || NdotV < 0.0)
        return float3(0.0, 0.0, 0.0);

    float NdotH = dot(N, H);
    float LdotH = dot(L, H);

    float luminance = 0.3 * baseColor.r + 0.6 * baseColor.g + 0.1 * baseColor.b;
    float3 C_tint = luminance > 0.0 ? baseColor / luminance : float3(1.0, 1.0, 1.0);
    float3 C_spec = lerp(specular * 0.08 * lerp(float3(1.0, 1.0, 1.0), C_tint, specularTint), baseColor, metallic);
    float3 C_sheen = lerp(float3(1.0, 1.0, 1.0), C_tint, sheenTint);

    // diffuse
    float F_i = SchlickFresnel(NdotL);
    float F_o = SchlickFresnel(NdotV);
    float F_d90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float F_d = lerp(1.0, F_d90, F_i) * lerp(1.0, F_d90, F_o);

    float F_ss90 = LdotH * LdotH * roughness;
    float F_ss = lerp(1.0, F_ss90, F_i) * lerp(1.0, F_ss90, F_o);
    float ss = 1.25 * (F_ss * (1.0 / (NdotL + NdotV) - 0.5) + 0.5);

    float FH = SchlickFresnel(LdotH);
    float3 F_sheen = FH * sheen * C_sheen;

    float3 BRDFdiffuse = ((1.0 / PI) * lerp(F_d, ss, subsurface) * baseColor + F_sheen) * (1.0 - metallic);

    // specular
    float aspect = sqrt(1.0 - anisotropic * 0.9);
    float roughness2 = roughness * roughness;
    float a_x = max(0.001, roughness2 / aspect);
    float a_y = max(0.001, roughness2 * aspect);
    float D_s = D_GTR2aniso(NdotH, dot(H, X), dot(H, Y), a_x, a_y);
    float3 F_s = lerp(C_spec, float3(1.0, 1.0, 1.0), FH);
    float G_s = G_GGXaniso(NdotL, dot(L, X), dot(L, Y), a_x, a_y) * G_GGXaniso(NdotV, dot(V, X), dot(V, Y), a_x, a_y);

    float3 BRDFspecular = G_s * F_s * D_s;

    // clearcoat
    float D_r = D_GTR1(NdotH, lerp(0.1, 0.001, clearcoatGloss));
    float F_r = lerp(0.04, 1.0, FH);
    float G_r = G_GGX(NdotL, 0.25) * G_GGX(NdotV, 0.25);

    float3 BRDFclearcoat = float3(0.25, 0.25, 0.25) * clearcoat * G_r * F_r * D_r;

    return BRDFdiffuse + BRDFspecular + BRDFclearcoat;
}


// 法線分布関数
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d);
}

// 幾何減衰関数 
float G_SchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // GGXのk
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float ggx_v = G_SchlickGGX(NdotV, roughness);
    float ggx_l = G_SchlickGGX(NdotL, roughness);
    return ggx_v * ggx_l;
}

// フレネル関数
float3 F_Schlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// 粗さを考慮したフレネル関数 
float3 F_SchlickRoughness(float NdotV, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(saturate(1.0 - NdotV), 5.0);
}