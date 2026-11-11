[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 0)]] Texture2DArray<float4> MonsterAtlas;
struct FormulaParameters {
    float4 CenterRadius;
    float4 Color;
    uint4 Mode;
    uint4 FrameTexels;
};
[vk::push_constant] FormulaParameters Formula;

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target {
    if (Formula.Mode.x == 0)
        return SceneColor.Load(int3(int2(position.xy), 0));

    float2 distance = (position.xy - Formula.CenterRadius.xy) / Formula.CenterRadius.zw;
    if (Formula.Mode.x == 3) {
        float2 local = position.xy - (Formula.CenterRadius.xy - Formula.CenterRadius.zw);
        float2 size = Formula.CenterRadius.zw * 2;
        float4 background = SceneColor.Load(int3(int2(position.xy), 0));
        if (any(local < 0) || any(local >= size)) return background;
        int2 texel = min(int2(floor(local * (float2(Formula.FrameTexels.zw) / size))), int2(Formula.FrameTexels.zw) - 1);
        if (Formula.Mode.y != 0) texel.x = int(Formula.FrameTexels.z) - 1 - texel.x;
        texel += int2(Formula.FrameTexels.xy);
        float4 color = MonsterAtlas.Load(int4(texel, 0, 0));
        return float4(lerp(background.rgb, color.rgb, color.a), 1);
    }
    if (Formula.Mode.x == 1 ? dot(distance, distance) > 1.0 : any(abs(distance) > 1.0))
        discard;
    return Formula.Color;
}
