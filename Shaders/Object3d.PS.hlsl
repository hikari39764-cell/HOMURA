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

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);
ConstantBuffer<CameraData> gCamera : register(b2);

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
        // 法線とライト方向の内積を求める
        float32_t3 normal = normalize(input.normal);
        float32_t3 lightDirection = normalize(gDirectionalLight.direction);
        float NdotL = dot(normal, -lightDirection);

        // Half Lambert。裏側も少し明るく見えるように[-1,1]を[0,1]へ変換する
        float halfLambert = pow(NdotL * 0.5f + 0.5f, 2.0f);

        float32_t3 diffuse =
            gMaterial.color.rgb *
            textureColor.rgb *
            gDirectionalLight.color.rgb *
            halfLambert *
            gDirectionalLight.intensity;

        // カメラへ向かう方向と反射方向から鏡面反射を求める
        float32_t3 toEye = SafeNormalize(gCamera.worldPosition - input.worldPosition);
        float32_t specularPower = 0.0f;
        if (gMaterial.shadingModel == 1)
        {
            float32_t3 reflected = reflect(lightDirection, normal);
            specularPower = pow(saturate(dot(reflected, toEye)), gMaterial.shininess);
        }
        else if (gMaterial.shadingModel == 2)
        {
            // Blinn Phongでは視線と光への方向の中間ベクトルを使う
            float32_t3 halfVector = SafeNormalize(-lightDirection + toEye);
            specularPower = pow(saturate(dot(normal, halfVector)), gMaterial.shininess);
        }

        float32_t3 specular = gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPower;
        output.color.rgb = diffuse + specular;
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
