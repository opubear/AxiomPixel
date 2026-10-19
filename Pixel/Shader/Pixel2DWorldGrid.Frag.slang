[[vk::binding(0, 0)]] Texture2D<uint> GridData;
[[vk::binding(1, 0)]] Texture2DArray<float4> Materials;
[[vk::binding(2, 0)]] SamplerState NearestSampler;

struct WorldConstants {
    int2 Origin;
    uint2 CellSize;
    float4 Background;
    uint LayerCount;
    uint Width;
    uint Height;
    uint MissingOutsideRegion;
    uint DisplayMode;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;
};
[vk::push_constant] WorldConstants View;

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    int2 q = View.Origin + int2(position.xy);
    int2 extent = int2(uint2(View.Width, View.Height) * View.CellSize);
    if (any(q < 0) || any(q >= extent)) {
        if (View.MissingOutsideRegion == 0) return View.Background;
        uint2 tile = uint2(position.xy) / max(View.CellSize, uint2(1, 1)) / 4;
        float shade = ((tile.x + tile.y) & 1u) != 0 ? .22 : .30;
        return float4(shade, shade, shade, 1);
    }
    uint2 cell = uint2(q) / View.CellSize;
    uint id = GridData.Load(int3(cell, 0));
    if (id == 0xffffffffu) {
        float shade = ((cell.x / 4 + cell.y / 4) & 1u) != 0 ? .22 : .30;
        return float4(shade, shade, shade, 1);
    }
    // Force-field frames reuse the R32UInt texture for packed linear RGB8.
    // Decode before material/empty-cell handling so the field fills all cells.
    if (View.DisplayMode == 1)
        return float4(float3(id & 255, (id >> 8) & 255, (id >> 16) & 255) / 255.0, 1.0);
    if (id == 0) return View.Background;
    if (id >= View.LayerCount) return float4(1, 0, 1, 1);
    float2 uv = (float2(uint2(q) % View.CellSize) + 0.5) / float2(View.CellSize);
    float4 color = Materials.SampleLevel(NearestSampler, float3(uv, float(id)), 0.0);
    return float4(color.rgb * color.a + View.Background.rgb * (1.0 - color.a), 1.0);
}
