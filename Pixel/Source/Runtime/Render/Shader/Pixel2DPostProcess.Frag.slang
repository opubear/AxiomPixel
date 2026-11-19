#include "PixelTonemap.slangh"

[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 0)]] SamplerState NearestSampler;
struct PostParameters {
    float4 ColorMultiplier;
    float4 Tonemapping; // x: bypass for diagnostic views or an already mapped image
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
    float3 graded = color.rgb * Post.ColorMultiplier.rgb;
    float3 mapped = Post.Tonemapping.x > 0.5 ? graded : TonemapACES(graded);
    // The sRGB presentation attachment performs the only display encoding.
    return float4(mapped, 1.0);
}
