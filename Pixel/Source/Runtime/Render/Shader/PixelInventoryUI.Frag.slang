[[vk::binding(0, 0)]] Texture2D<float4> Canvas;
struct UIParameters { float4 Bounds; };
[vk::push_constant] UIParameters UI;

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    uint width, height;
    Canvas.GetDimensions(width, height);
    // Snap exact texel boundaries consistently at fractional window scales.
    float2 logical = (position.xy - UI.Bounds.xy) * (float2(width, height) / UI.Bounds.zw);
    int2 texel = int2(floor(logical + 0.0001));
    float4 color = Canvas.Load(int3(clamp(texel, int2(0), int2(width - 1, height - 1)), 0));
    if (color.a < 0.5) discard;
    return color;
}
