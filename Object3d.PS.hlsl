#include "Object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t3 padding;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float32_t4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    if (gMaterial.enableLighting != 0)
    {
        // 法線とライト方向から、どれくらい光が当たっているかを求める
        float cos = saturate(dot(normalize(input.normal), -gDirectionalLight.direction));

        output.color =
            gMaterial.color *
            textureColor *
            gDirectionalLight.color *
            cos *
            gDirectionalLight.intensity;
    }
    else
    {
        // Lightingしない場合は、今まで通りTextureとMaterialColorだけを使う
        output.color = gMaterial.color * textureColor;
    }

    return output;
}