#include "Sprite.hlsli"
Texture2D diffuse_map : register(t0);
SamplerState diffuse_map_sampler_state : register(s0);
float4 main(VS_OUT pin) : SV_TARGET
{
	float4 color = diffuse_map.Sample(diffuse_map_sampler_state, pin.texcoord);
	float alpha = color.a;

#if 1
	const float GAMMA = 2.2;
	color.rgb = pow(color.rgb, GAMMA);
#endif
	return float4(color.rgb, alpha) * pin.color;
}