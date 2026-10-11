#include "Particle.hlsli"

struct ParticleForGPU
{
	float32_t4x4 WVP;
	float32_t4x4 World;
	float32_t4 color;
};

StructuredBuffer<ParticleForGPU> gParticles : register(t0);

struct VertexShaderInput
{
	float32_t4 position : POSITION0;
	float32_t2 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input, uint32_t instanceId : SV_InstanceID)
{
	VertexShaderOutput output;
	output.position = mul(input.position, gParticles[instanceId].WVP);
	output.texcoord = input.texcoord;
	output.color = gParticles[instanceId].color;
	return output;
}
