[[vk::binding(0, 0)]] Texture2D<uint> GridData;
[[vk::binding(1, 0)]] Texture2DArray<float4> Materials;
[[vk::binding(2, 0)]] SamplerState NearestSampler;
[[vk::binding(3, 0)]] Texture2DArray<float4> BackgroundLayers;

struct WorldConstants {
    float2 Origin;
    float2 CellSize;
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
    float4 BiomeWeights;
    float4 BiomePhases;
};
[vk::push_constant] WorldConstants View;

float4 sampleBackground(float2 position, uint layer, float phase) {
    uint width, height, count;
    BackgroundLayers.GetDimensions(width, height, count);
    float2 halfTexel = 0.5 / float2(width, height);
    float u = (position.x - View.BackgroundSize.z * 0.5) / View.BackgroundSize.x + 0.5 + phase;
    // Mirrored pairs hide source edges; keep UV inside the repeating sampler.
    u = 1.0 - abs(frac(u * 0.5) * 2.0 - 1.0);
    float2 uv = clamp(float2(u, saturate(position.y / View.BackgroundSize.y)), halfTexel, 1.0 - halfTexel);
    return BackgroundLayers.SampleLevel(NearestSampler, float3(uv, float(layer)), 0.0);
}

float4 backgroundColor(float2 position) {
    if (View.BackgroundSize.w == 0) return float4(View.Background.rgb, 0);
    float3 forest = View.Background.rgb;
    if (View.BiomeWeights.x > 0) {
        for (uint layer = 0; layer < uint(View.BackgroundSize.w); ++layer) {
            float4 sample = sampleBackground(position, layer, View.BackgroundPhases[layer]);
            forest = lerp(forest, sample.rgb, sample.a);
        }
    }
    float3 color = forest * View.BiomeWeights.x;
    for (uint biome = 0; biome < 2; ++biome) {
        if (View.BiomeWeights[biome + 1] <= 0) continue;
        float4 sample = sampleBackground(position, uint(View.BackgroundSize.w) + biome, View.BiomePhases[biome]);
        color += lerp(View.Background.rgb, sample.rgb, sample.a) * View.BiomeWeights[biome + 1];
    }
    return float4(color, 0);
}

float4 torchColor(float2 pixel, float2 withinCell, bool lit) {
    // A fixed 8×8 sprite in cell space, independent of terrain tiling.
    int2 q = int2(withinCell * 8.0);
    if (lit) {
        if ((q.y == 1 && q.x == 4) || (q.y == 2 && q.x >= 2 && q.x <= 5)
            || (q.y == 3 && q.x >= 2 && q.x <= 5))
            return q.x == 4 && q.y >= 2 ? float4(1, .64, .18, 1) : float4(1, .28, .02, 1);
    } else if (q.y == 3 && q.x >= 3 && q.x <= 4) {
        return float4(.11, .09, .07, 1);
    }
    if (q.y >= 4 && q.x == 3) return float4(.17, .072, .026, 1);
    if (q.y >= 4 && q.x == 4) return float4(.39, .19, .064, 1);
    return backgroundColor(pixel);
}

[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target
{
    float2 q = View.Origin + position.xy;
    float2 extent = float2(View.Width, View.Height) * View.CellSize;
    if (any(q < 0) || any(q >= extent)) {
        if (View.MissingOutsideRegion == 0) return backgroundColor(position.xy);
        uint2 tile = uint2(position.xy / View.CellSize / 4);
        float shade = ((tile.x + tile.y) & 1u) != 0 ? .22 : .30;
        return float4(shade, shade, shade, 0);
    }
    uint2 cell = uint2(floor(q / View.CellSize));
    uint id = GridData.Load(int3(cell, 0));
    if (id == 0xffffffffu) {
        float shade = ((cell.x / 4 + cell.y / 4) & 1u) != 0 ? .22 : .30;
        return float4(shade, shade, shade, 0);
    }
    // Force-field frames reuse the R32UInt texture for packed linear RGB8.
    // Decode before material/empty-cell handling so the field fills all cells.
    if (View.DisplayMode == 1)
        return float4(float3(id & 255, (id >> 8) & 255, (id >> 16) & 255) / 255.0, 1.0);
    if (id == 0x0fffffffu || id == 0x0ffffffeu)
        return torchColor(position.xy, frac(q / View.CellSize), id == 0x0fffffffu);
    bool surface = (id & 0x80000000u) != 0;
    uint surfaceStrip = (id >> 28) & 7u;
    id &= 0x0fffffffu;
    if (id == 0) return backgroundColor(position.xy);
    if (id >= View.LayerCount) return float4(1, 0, 1, 1);
    uint period = max(View.CellsPerTile, 1u);
    float2 withinCell = frac(q / View.CellSize);
    uint2 phase = uint2(View.TerrainPhaseX, View.TerrainPhaseY);
    float2 uv = (float2((cell % period + phase) % period) + withinCell) / float(period);
    // Select the configured green/foam strip in the exposed physical cell.
    // A vertical world phase could sample soil or the water image's empty sky.
    if (surface) uv.y = (float(surfaceStrip) + withinCell.y) * 0.125;
    float4 color = Materials.SampleLevel(NearestSampler, float3(uv, float(id)), 0.0);
    if (color.a == 1.0) return color;
    float4 background = backgroundColor(position.xy);
    return float4(color.rgb * color.a + background.rgb * (1.0 - color.a), color.a);
}
