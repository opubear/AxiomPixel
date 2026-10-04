[[vk::binding(0, 0)]] Texture2D<uint> GridData0;
[[vk::binding(1, 0)]] Texture2D<uint> GridData1;
[[vk::binding(2, 0)]] Texture2D<uint> GridData2;
[[vk::binding(3, 0)]] Texture2D<uint> GridData3;
[[vk::binding(4, 0)]] Texture2DArray<float4> Materials;
[[vk::binding(5, 0)]] SamplerState NearestSampler;

struct WorldConstants {
    int2 Origin;
    uint2 CellSize;
    float4 Background;
    uint LayerCount;
    uint Width;
    uint Height;
    uint PageHeight;
    uint DisplayMode;
    uint Reserved0;
    uint Reserved1;
    uint Reserved2;
};
[vk::push_constant] WorldConstants View;

uint ReadId(uint2 cell)
{
    int3 coord = int3(cell.x, cell.y % View.PageHeight, 0);
    switch (cell.y / View.PageHeight) {
    case 0: return GridData0.Load(coord);
    case 1: return GridData1.Load(coord);
    case 2: return GridData2.Load(coord);
    default: return GridData3.Load(coord);
    }
}
[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    int2 q = View.Origin + int2(position.xy);
    int2 extent = int2(uint2(View.Width, View.Height) * View.CellSize);
    if (any(q < 0) || any(q >= extent)) return View.Background;
    uint2 cell = uint2(q) / View.CellSize;
    uint id = ReadId(cell);
    // Force-field frames reuse the R32UInt pages for packed linear RGB8.
    // Decode before material/empty-cell handling so the field fills all cells.
    if (View.DisplayMode == 1)
        return float4(float3(id & 255, (id >> 8) & 255, (id >> 16) & 255) / 255.0, 1.0);
    if (id == 0) return View.Background;
    if (id >= View.LayerCount) return float4(1, 0, 1, 1);
    float2 uv = (float2(uint2(q) % View.CellSize) + 0.5) / float2(View.CellSize);
    float4 color = Materials.SampleLevel(NearestSampler, float3(uv, float(id)), 0.0);
    return float4(color.rgb * color.a + View.Background.rgb * (1.0 - color.a), 1.0);
}
