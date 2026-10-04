[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 0)]] SamplerState NearestSampler;
struct PostParameters {
    float4 ColorMultiplier;
    float4 Reserved0;
    float4 Reserved1;
    float4 Reserved2;
};
[vk::push_constant] PostParameters Post;
[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    uint width, height;
    SceneColor.GetDimensions(width, height);
    float4 color = SceneColor.SampleLevel(NearestSampler, position.xy / float2(width, height), 0.0);
    return float4(color.rgb * Post.ColorMultiplier.rgb, 1.0);
}
