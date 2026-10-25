[[vk::binding(0, 0)]] Texture2D<uint> GridData;
[[vk::binding(1, 0)]] Texture2DArray<float4> Materials;
[[vk::binding(2, 0)]] SamplerState NearestSampler;
[[vk::binding(3, 0)]] Texture2DArray<float4> BackgroundLayers;

struct WorldConstants {
    int2 Origin;
    uint2 CellSize;
    float4 Background;
    uint LayerCount;
    uint Width;
    uint Height;
    uint MissingOutsideRegion;
    uint DisplayMode;
    uint CellsPerTile;
    uint TerrainPhaseX;
    uint TerrainPhaseY;
    float4 BackgroundPhases;
    float4 BackgroundSize;
};
[vk::push_constant] WorldConstants View;

float4 backgroundColor(float2 position) {
    float4 color = View.Background;
    uint width, height, count;
    BackgroundLayers.GetDimensions(width, height, count);
    float2 halfTexel = 0.5 / float2(width, height);
    for (uint layer = 0; layer < uint(View.BackgroundSize.w); ++layer) {
        float u = (position.x - View.BackgroundSize.z * 0.5) / View.BackgroundSize.x
                  + 0.5 + View.BackgroundPhases[layer];
        // Mirrored pairs hide the non-seamless source edges. Clamp Y so the
        // transparent layers never repeat vertically into the sky.
        u = 1.0 - abs(frac(u * 0.5) * 2.0 - 1.0);
        float2 uv = float2(u, saturate(position.y / View.BackgroundSize.y));
        // The shared terrain sampler repeats. Keep both edge coordinates
        // inside the image so v/u == 1 cannot wrap to the opposite edge.
        uv = clamp(uv, halfTexel, 1.0 - halfTexel);
        float4 sample = BackgroundLayers.SampleLevel(NearestSampler, float3(uv, float(layer)), 0.0);
        color.rgb = sample.rgb * sample.a + color.rgb * (1.0 - sample.a);
    }
    return color;
}

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    int2 q = View.Origin + int2(position.xy);
    int2 extent = int2(uint2(View.Width, View.Height) * View.CellSize);
    if (any(q < 0) || any(q >= extent)) {
        if (View.MissingOutsideRegion == 0) return backgroundColor(position.xy);
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
    bool surface = (id & 0x80000000u) != 0;
    uint surfaceStrip = (id >> 28) & 7u;
    id &= 0x0fffffffu;
    if (id == 0) return backgroundColor(position.xy);
    if (id >= View.LayerCount) return float4(1, 0, 1, 1);
    uint period = max(View.CellsPerTile, 1u);
    float2 withinCell = (float2(uint2(q) % View.CellSize) + 0.5) / float2(View.CellSize);
    uint2 phase = uint2(View.TerrainPhaseX, View.TerrainPhaseY);
    float2 uv = (float2((cell % period + phase) % period) + withinCell) / float(period);
    // Select the configured green/foam strip in the exposed physical cell.
    // A vertical world phase could sample soil or the water image's empty sky.
    if (surface) uv.y = (float(surfaceStrip) + withinCell.y) * 0.125;
    float4 color = Materials.SampleLevel(NearestSampler, float3(uv, float(id)), 0.0);
    if (color.a == 1.0) return color;
    float4 background = backgroundColor(position.xy);
    return float4(color.rgb * color.a + background.rgb * (1.0 - color.a), 1.0);
}
