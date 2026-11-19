#include "PixelTonemap.slangh"

[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 0)]] Texture2D<float4> Lightmap;

struct LightPostParameters {
    float4 Ambient;
    float4 Tint;
    float4 Grid; // Grid-relative pixel origin, samples per pixel, light decode scale
};
[vk::push_constant] LightPostParameters Post;

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    const float4 scene = SceneColor.Load(int3(int2(position.xy), 0));
    float3 local = 0;
    if (scene.a > 0) {
        uint width, height;
        Lightmap.GetDimensions(width, height);
        const float2 uv = (Post.Grid.xy + position.xy) * Post.Grid.z;
        if (all(uv >= 0.0) && all(uv < float2(width, height)))
            local = Lightmap.Load(int3(int2(uv), 0)).rgb * Post.Grid.w;
    }
    const float3 color = scene.rgb * (Post.Ambient.rgb + scene.a * local * Post.Tint.rgb);
    return float4(TonemapACES(color), 1);
}
