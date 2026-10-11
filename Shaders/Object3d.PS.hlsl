#include "Object3d.hlsli"

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t alphaCutoff;
    float32_t shininess;
    int32_t shadingModel;
    float32_t4x4 uvTransform;
};

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};

struct CameraData
{
    float32_t3 worldPosition;
    float32_t padding;
};

struct PointLight
{
    float32_t4 color;
    float32_t3 position;
    float32_t intensity;
    float32_t radius;
    float32_t decay;
    float32_t2 padding;
};

struct SpotLight
{
    float32_t4 color;
    float32_t3 position;
    float32_t intensity;
    float32_t3 direction;
    float32_t distance;
    float32_t decay;
    float32_t cosAngle;
    float32_t cosFalloffStart;
    float32_t padding;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<CameraData> gCamera : register(b2);
ConstantBuffer<PointLight> gPointLight : register(b3);
ConstantBuffer<SpotLight> gSpotLight : register(b4);

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

float32_t3 SafeNormalize(float32_t3 value)
{
    // 同じ位置や正反対の方向でも0除算しないようにする
    return value * rsqrt(max(dot(value, value), 0.000001f));
}

float32_t3 CalculateLighting(float32_t3 baseColor, float32_t3 normal, float32_t3 toEye,
    float32_t3 lightDirection, float32_t3 lightColor, float32_t intensity)
{
    float NdotL = dot(normal, -lightDirection);
    float halfLambert = pow(NdotL * 0.5f + 0.5f, 2.0f);
    float32_t3 diffuse = baseColor * lightColor * halfLambert * intensity;

    // 光源の種類によらず、同じ反射モデルで鏡面反射を求める
    float32_t specularPower = 0.0f;
    if (gMaterial.shadingModel == 1)
    {
        float32_t3 reflected = reflect(lightDirection, normal);
        specularPower = pow(saturate(dot(reflected, toEye)), gMaterial.shininess);
    }
    else if (gMaterial.shadingModel == 2)
    {
        float32_t3 halfVector = SafeNormalize(-lightDirection + toEye);
        specularPower = pow(saturate(dot(normal, halfVector)), gMaterial.shininess);
    }
    float32_t3 specular = lightColor * intensity * specularPower;
    return diffuse + specular;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // MaterialのUVTransformでTexture座標を動かす
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 透明な部分は深度も書き込まないように描画を中止する
    if (textureColor.a <= gMaterial.alphaCutoff)
    {
        discard;
    }

    if (gMaterial.enableLighting != 0)
    {
        // 平行光源の反射を求める
        float32_t3 normal = normalize(input.normal);
        float32_t3 lightDirection = normalize(gDirectionalLight.direction);
        float32_t3 toEye = SafeNormalize(gCamera.worldPosition - input.worldPosition);
        float32_t3 baseColor = gMaterial.color.rgb * textureColor.rgb;
        output.color.rgb = CalculateLighting(baseColor, normal, toEye, lightDirection,
            gDirectionalLight.color.rgb, gDirectionalLight.intensity);

        // 点光源からの距離で、拡散反射と鏡面反射を一緒に減衰させる
        float32_t3 pointVector = input.worldPosition - gPointLight.position;
        float32_t pointDistance = length(pointVector);
        float32_t pointFactor = pow(saturate(1.0f - pointDistance / max(gPointLight.radius, 0.001f)), gPointLight.decay);
        output.color.rgb += CalculateLighting(baseColor, normal, toEye, SafeNormalize(pointVector),
            gPointLight.color.rgb, gPointLight.intensity) * pointFactor;

        // スポットライトは距離と照射角度の両方で減衰させる
        float32_t3 spotVector = input.worldPosition - gSpotLight.position;
        float32_t3 spotDirectionOnSurface = SafeNormalize(spotVector);
        float32_t spotDistance = length(spotVector);
        float32_t attenuation = pow(saturate(1.0f - spotDistance / max(gSpotLight.distance, 0.001f)), gSpotLight.decay);
        float32_t angleCos = dot(spotDirectionOnSurface, SafeNormalize(gSpotLight.direction));
        float32_t falloff = saturate((angleCos - gSpotLight.cosAngle) / max(gSpotLight.cosFalloffStart - gSpotLight.cosAngle, 0.001f));
        output.color.rgb += CalculateLighting(baseColor, normal, toEye, spotDirectionOnSurface,
            gSpotLight.color.rgb, gSpotLight.intensity) * attenuation * falloff;
    }
    else
    {
        // Lightingしない場合は、今まで通りTextureとMaterialColorだけを使う
        output.color.rgb = gMaterial.color.rgb * textureColor.rgb;
    }

    // 透明度はライトの強さに影響されないように別に計算する
    output.color.a = gMaterial.color.a * textureColor.a;
    if (output.color.a <= 0.0f)
    {
        discard;
    }

    return output;
}
