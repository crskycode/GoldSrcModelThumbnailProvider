cbuffer MatrixBuffer : register(b0)
{
    float4x4 World; // World matrix
    float4x4 View; // View matrix
    float4x4 Projection; // Projection matrix
};

cbuffer BoneBuffer : register(b1)
{
    float4x4 BoneTransforms[128]; // 128 bone transformation matrices
};

struct VertexInput
{
    float3 Position : POSITION; // Vertex position
    float3 Normal : NORMAL; // Vertex normal
    float2 TexCoord : TEXCOORD; // Texture coordinates
    uint BoneIndex : BLENDINDICES; // Index of the bone the vertex is bound to
};

struct VertexOutput
{
    float4 Position : SV_POSITION; // Transformed vertex position
    float3 Normal : NORMAL; // Transformed vertex normal
    float2 TexCoord : TEXCOORD; // Texture coordinates
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;

    // Get the corresponding bone transformation matrix
    float4x4 boneTransform = BoneTransforms[input.BoneIndex];

    // Transform from model space to world space
    float4 worldPosition = mul(float4(input.Position, 1.0), boneTransform);
    
    // Combine with bone transformation
    worldPosition = mul(worldPosition, World);

    // Transform from world space to clip space
    output.Position = mul(worldPosition, View);
    output.Position = mul(output.Position, Projection);

    // Transform normal
    float3x3 normalMatrix = (float3x3) World; // Extract normal matrix
    output.Normal = normalize(mul(input.Normal, normalMatrix)); // Transform and normalize the normal

    // Pass through texture coordinates
    output.TexCoord = input.TexCoord;

    return output;
}
