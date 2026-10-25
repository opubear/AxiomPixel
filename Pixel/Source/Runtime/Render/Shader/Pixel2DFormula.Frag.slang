[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
struct FormulaParameters {
    float4 CenterRadius;
    float4 Color;
    uint4 Mode;
};
[vk::push_constant] FormulaParameters Formula;

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target {
    if (Formula.Mode.x == 0)
        return SceneColor.Load(int3(int2(position.xy), 0));

    float2 distance = (position.xy - Formula.CenterRadius.xy) / Formula.CenterRadius.zw;
    if (Formula.Mode.x == 1 ? dot(distance, distance) > 1.0 : any(abs(distance) > 1.0))
        discard;
    return Formula.Color;
}
