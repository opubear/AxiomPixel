[[vk::binding(0, 0)]] Texture2D<float4> SceneColor;
[[vk::binding(1, 0)]] Texture2D<uint> WindField;
struct WindConstants {
    int2 Origin;
    uint2 CellSize;
    uint2 Extent;
    uint Mode;
    float Time;
    float Opacity;
    float RefractionPixels;
    float MotionScale;
    float Reserved;
};
[[vk::push_constant]] WindConstants Wind;

// Mirrors WindVisualFrame: signed fixed-point XY, solid sentinel, calm outside.
float3 ReadWind(int2 cell) {
    if (any(cell < 0) || any(cell >= int2(1024, 1024))) return float3(0);
    uint packed = WindField.Load(int3(cell, 0));
    if (packed == 0x80008000u) return float3(0);
    int2 bits = int2(packed & 65535u, packed >> 16);
    int2 signedValue = int2(bits.x >= 32768 ? bits.x - 65536 : bits.x,
                           bits.y >= 32768 ? bits.y - 65536 : bits.y);
    return float3(float2(signedValue) / 32.0, 1);
}
float3 SampleWind(float2 p) {
    if (ReadWind(int2(floor(p))).z == 0) return float3(0);
    float2 q = p - .5;
    int2 c = int2(floor(q));
    float2 f = frac(q);
    // Zero-valued solid samples feather velocity at the air boundary.
    return lerp(lerp(ReadWind(c), ReadWind(c + int2(1, 0)), f.x),
                lerp(ReadWind(c + int2(0, 1)), ReadWind(c + int2(1, 1)), f.x), f.y);
}
float2 VisualVelocity(float2 velocity) {
    float2 scaled = velocity * Wind.MotionScale;
    // Bounded travel keeps all 32 backtrace steps shorter than one cell.
    return scaled / (1 + length(scaled) / 12.0);
}
float2 SeedOffset(int2 cell, uint layer) {
    uint hash = uint(cell.x) * 1664525u + uint(cell.y) * 1013904223u + 374761393u + layer * 2246822519u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    return float2(hash & 65535u, hash >> 16) / 65535.0;
}
float StreakMask(float2 p, float2 direction, float strength, uint layer) {
    const float spacing = 6.0;
    int2 tile = int2(floor(p / spacing));
    float mask = 0;
    float2 normal = float2(-direction.y, direction.x);
    float pixelWidth = 1.0 / min(Wind.CellSize.x, Wind.CellSize.y);
    float width = Wind.Mode == 1 ? max(.055, pixelWidth * .65) : max(.35 + .25 * strength, pixelWidth);
    float tail = Wind.Mode == 1 ? .6 + 1.8 * strength : 1.3 + 2.2 * strength;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 cell = tile + int2(x, y);
            float2 seed = (float2(cell) + .25 + .5 * SeedOffset(cell, layer)) * spacing;
            float2 delta = p - seed;
            float along = dot(delta, direction);
            float across = dot(delta, normal) / width;
            // A long fading tail and a soft, short leading edge give direction.
            float longitudinal = along / (along < 0 ? tail : width * 1.5);
            mask = max(mask, exp(-2.0 * (across * across + longitudinal * longitudinal)));
        }
    }
    return mask;
}
float AdvectedMask(float2 p, float age, float strength, uint layer) {
    float2 q = p;
    float3 field = SampleWind(q);
    for (int i = 0; i < 32; ++i) {
        q -= VisualVelocity(field.xy) * (age / 32.0);
        field = ReadWind(int2(floor(q)));
        if (field.z < .01 || length(field.xy) < .03) return 0;
    }
    return StreakMask(q, normalize(SampleWind(q).xy), strength, layer);
}
float4 SceneTap(int2 pixel, float4 fallback) {
    float2 world = (float2(Wind.Origin) + float2(pixel) + .5) / float2(Wind.CellSize);
    return ReadWind(int2(floor(world))).z > 0 ? SceneColor.Load(int3(pixel, 0)) : fallback;
}
float4 SampleScene(float2 pixel, float4 fallback) {
    float2 q = clamp(pixel - .5, float2(0), float2(Wind.Extent) - 1);
    int2 c = int2(floor(q));
    int2 next = min(c + 1, int2(Wind.Extent) - 1);
    float2 f = frac(q);
    return lerp(lerp(SceneTap(c, fallback), SceneTap(int2(next.x, c.y), fallback), f.x),
                lerp(SceneTap(int2(c.x, next.y), fallback), SceneTap(next, fallback), f.x), f.y);
}
[shader("fragment")]
float4 main(float4 position : SV_Position) : SV_Target {
    float4 background = SceneColor.Load(int3(int2(position.xy), 0));
    float2 p = (float2(Wind.Origin) + position.xy) / float2(Wind.CellSize);
    float3 field = SampleWind(p);
    float speed = length(field.xy);
    if (field.z < .01 || speed < .03) return background;
    float strength = speed / (speed + 60.0);
    float2 direction = field.xy / speed;
    // Independent seed layers avoid correlated crossfades that can appear to
    // travel backwards at low speed. Smooth phases hide bounded-trace resets.
    float phase = frac(Wind.Time * .5);
    float blend = .5 - .5 * cos(phase * 6.2831853);
    float mask = AdvectedMask(p, phase * 2.0, strength, 0) * blend
               + AdvectedMask(p, frac(phase + .5) * 2.0, strength, 1) * (1 - blend);
    if (Wind.MotionScale == 0) mask = StreakMask(p, direction, strength, 0);
    mask *= smoothstep(.03, 2.0, speed) * field.z;
    float3 color = background.rgb;
    if (Wind.Mode == 2) {
        float2 normal = float2(-direction.y, direction.x);
        float2 displacement = (normal + direction * .25) * Wind.RefractionPixels * strength * mask;
        float2 displacedWorld = p + displacement / float2(Wind.CellSize);
        // Never pull solid foreground or outside-world colour into the air.
        if (ReadWind(int2(floor(displacedWorld))).z > 0)
            color = lerp(color, SampleScene(position.xy + displacement, background).rgb, Wind.Opacity);
    }
    // A small neutral cue remains legible on flat sky where refraction alone
    // has nothing to displace. No paired highlight/shadow or hard ribbon edge.
    float luminance = dot(background.rgb, float3(.2126, .7152, .0722));
    float contrast = luminance < .5 ? 1 : 0;
    float opacity = Wind.Opacity * mask * sqrt(strength) * (Wind.Mode == 1 ? .85 : .45);
    return float4(lerp(color, float3(contrast), opacity), 1);
}
