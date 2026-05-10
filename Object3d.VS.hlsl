struct VertexShaderInput
{
    float32_t4 position : POSITION0;
};

struct VertexShaderOutput
{
    float32_t4 position : SV_POSITION;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = input.position;
    return output;
}