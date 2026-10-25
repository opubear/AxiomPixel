[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
struct CharacterParameters {
    float4 Bounds;
    float4 HeadColor;
    float4 BodyColor;
    float4 Parameters;
};
[vk::push_constant] CharacterParameters Character;

// Exact area covered by an axis-aligned rectangle in this framebuffer pixel.
// Fractional coverage makes subpixel movement continuous without blurring terrain.
float coverage(float2 pixelCentre, float2 minimum, float2 maximum) {
    float2 overlap = max(0.0, min(pixelCentre + 0.5, maximum) - max(pixelCentre - 0.5, minimum));
    return overlap.x * overlap.y;
}
[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target {
    float4 background = SceneColor.Load(int3(int2(position.xy), 0));
    float2 minimum = Character.Bounds.xy;
    float2 maximum = minimum + Character.Bounds.zw;
    float split = minimum.y + Character.Bounds.w * Character.Parameters.x;
    float head = coverage(position.xy, minimum, float2(maximum.x, split));
    float body = coverage(position.xy, float2(minimum.x, split), maximum);
    return float4(background.rgb * max(0.0, 1.0 - head - body)
                  + Character.HeadColor.rgb * head + Character.BodyColor.rgb * body, 1.0);
}
