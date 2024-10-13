Texture2D defaultTexture : register(t0); // Texture input
SamplerState samplerState : register(s0); // Sampler state

struct PixelInput
{
    float4 Position : SV_POSITION; // Transformed vertex position
    float3 Normal : NORMAL; // Transformed vertex normal
    float2 TexCoord : TEXCOORD; // Texture coordinates
};

float4 main(PixelInput input) : SV_Target
{
    // Sample color from the texture
    float4 textureColor = defaultTexture.Sample(samplerState, input.TexCoord);
    
    return textureColor; // Return the texture color
}
