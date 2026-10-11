#include "Particle.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float32_t4 main(VertexShaderOutput input) : SV_TARGET0
{
	float32_t4 color = input.color * gTexture.Sample(gSampler, input.texcoord);
	// 完全に透明な部分は描画しない
	if (color.a == 0.0f) {
		discard;
	}
	return color;
}
