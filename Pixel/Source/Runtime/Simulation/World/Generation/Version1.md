# Generation version 1 contract

`WorldGenerationSettings` is the complete configurable description: `Kind`,
`Version`, `Seed`, `SurfaceY`, and `WaterEnabled`. This document describes
`Version=1` for both generator kinds. Natural also supports version 2; see
[the biome generation contract](../../../../../../Docs/BiomeWorldGeneration.md).
Unknown versions/kinds throw. `Flat=0` and `Natural=1`
are stable kind IDs. Everything below is frozen by version 1 and requires a new
version if changed. No runtime parameter silently changes these constants.

The fingerprint is FNV-1a-64, offset `14695981039346656037`, multiplier
`1099511628211`, over little-endian fields in the order kind/u32, version/u32,
seed/u64, two's-complement SurfaceY/u64, water-enabled/u8. It includes every field
even when a particular generator does not use that field. Seed 1, natural,
surface 512, water enabled has fingerprint `3760764308338698577`.

## Coordinates and noise

Positive Y points down. The first solid in a column is H(x), and depth is
`y-H(x)`. Public generation coordinates are strictly inside
`[-2^60+4096, 2^60-4096]`; the margin leaves all feature lookups safe. Chunk
multiplication and rectangle endpoints are validated before use. A Chunk's edge
only partitions output; no feature size depends on it.

Noise uses Q16.16, with `FixedOne=65536`. Multiplication is
`floor(int64(a)*b/65536)` with explicit floor division. Rounding is
`floor((value+32768)/65536)`. Coordinates are split into floor-divided lattice
coordinates and integer remainders before adding the half-cell offset. Each
scale is a power of two in [8,16384]. The fractional coordinate is exactly
`((remainder*2+1)*32768)/scale`.

Fade evaluates t2=t*t, t3=t2*t, t4=t3*t, t5=t4*t with the above multiplication,
then clamps `6*t5-15*t4+10*t3` to [0,65536]. Interpolation is `a+mul(b-a,t)`:
first X at the top and bottom, then Y. Gradients, selected by hash modulo 8, are
`(+1,0),(-1,0),(0,+1),(0,-1),(+1,+1),(+1,-1),(-1,+1),(-1,-1)`.
The normalization coefficient is exactly 1 and final noise clamps to [-65536,65536].
Gradient dot/interpolation differences are bounded by 4*65536; all products fit
int64. Fade terms fit int32. Only bounded noise values and clamped depth ramps
enter subsequent products; distant world coordinates never enter a product with
a noise amplitude or floating frequency.

`StableMix(v)` performs `(v^(v>>30))*0xbf58476d1ce4e5b9`, then
`(v^(v>>27))*0x94d049bb133111eb`, then `v^(v>>31)`, all modulo 2^64.
`ChannelSeed(seed,tag)=StableMix(seed XOR FNV1a64(ASCII tag))`.
`FeatureHash(channel,x,y)=StableMix(channel XOR
StableMix(uint64(x)+0x9e3779b97f4a7c15) XOR
rotl(StableMix(uint64(y)+0xd1b54a32d192ed03),32))`.
Signed lattice coordinates use the defined uint64 conversion. The same hash is
used for gradients and feature placement, with independent named channels.

Golden vectors in PixelGenerationTests cover negative lattice coordinates,
anisotropic scales, ±2^55 coordinates, the fingerprint, and a material-region
digest. They have been checked on the current host; cross-platform execution
remains a release validation requirement.

## Flat

Flat emits Air for `y<SurfaceY`, Dirt for `y>=SurfaceY`, regardless of seed and
water setting. It preserves the legacy default Air/Dirt split at 512 exactly.

## Natural stages

All noise channels below append the exact `-v1` suffix shown in the source.

1. Raw H is `SurfaceY+round(48*N(height0,1024)+12*N(height1,256)+4*N(height2,64))`,
   using the Y=0 cell-centre slice. Tags are `terrain-height-0-v1`,
   `terrain-height-1-v1`, `terrain-height-2-v1`. Dry height lies within
   SurfaceY±64. At x=512 sample the raw anchor height; distance <=16 uses that
   height, distance >=80 uses raw height, and intermediate distance uses the
   fixed fade of `(distance-16)/64` to interpolate anchor to raw height.
2. Soil thickness is `clamp(18+round(6*N(terrain-soil-v1,256)),12,24)`.
   Above final H is Air, the next T cells are GroundSoil, then Stone.
3. Only Stone at depth >=48 can become cave Air. At depth 48..96 the strength
   is `((depth-48)*65536)/48`, then 65536 thereafter. Room threshold is
   `65536-mul(65536-29491,strength)`; rooms use `cave-room-v1` at 128x128 and a
   strict greater-than comparison. Tunnel width is `mul(3932,strength)`;
   `abs(cave-tunnel-v1(256x64))<width` and `cave-gate-v1(512x512)>0` creates
   passages. Lake protection prevents cave carving in initial lake support.
4. Only remaining Stone can become ore. Every ore has independent
   `<name>-region-v1` and `<name>-cluster-v1` fields, with strict greater-than
   thresholds. All region scales are 512x512. Priority is Gold, Tungsten,
   Iron, Copper, Coal; the first match wins. Depth thresholds remain constant
   at all greater depths.

| Ore | Min depth | Cluster scale | Region threshold /65536 | Cluster threshold /65536 |
| --- | ---: | --- | ---: | ---: |
| coal | 40 | 32x16 | -6554 | 18350 |
| copper | 80 | 32x16 | 0 | 20972 |
| iron | 160 | 32x32 | 3277 | 22938 |
| tungsten | 384 | 16x32 | 6554 | 26214 |
| gold | 320 | 16x16 | 9830 | 29491 |

5. Only Air becomes Water, from the finite plans below. Water disabled skips
   both lake-basin planning and water filling, giving the uncut dry terrain.
6. Only `y==H(x)` GroundSoil with dry Air above becomes GrassSoil. Underground
   water is at least 96 below every window column, so it cannot affect topsoil.
   Submerged lake beds retain GroundSoil. This stage does not grow new grass
   after world edits or simulation.
7. Generation changes only Materials. WorldChunk initializes direction (0,1)
   and force (0,3600), and these remain unchanged for all materials. Recount is
   performed on complete output; new chunks have Dirty=false.

## Finite water plans

Features occupy 512-cell intervals/cells with origin offset 256 in X and Y.
`feature=floor((world-256)/512)`. Plans never read loaded chunks and never keep
an unbounded world cache.

Lake hash uses `lake-placement-v1(featureX,0)`. Centre is feature origin+256+
`hash%129-64`; radius is `16+(hash>>8)%49`; target depth is
`2+(hash>>16)%7`. `lake-wetness-v1` at 2048x2048, Y=0, must exceed -8192.
The support interval includes an eight-cell border, lies wholly inside its
feature, and must not intersect x=512±144. Shores differ by at most eight cells
and the entire interval has dry-height range at most 12. Level L is
`max(dryLeft,dryRight)+1`. The bed at x uses fade weight
`fade((radius-abs(x-centre))/radius)` and cuts downward by
`round(weight*max(0,L+D(x)-Hdry(x)))`. D(x) is target depth plus rounded
`lake-depth-v1` noise at 64x64, clamped to [2,8]. All cuts must be <=24 and
all water depths <=8. The wet columns form one nonempty contiguous interval
of at least eight columns. Water occupies exactly `L<=y<H(x)`. The wet bounds
extend by eight cells left/right/below for cave protection, without filling
pre-existing Air with solid.

Groundwater uses hash `groundwater-placement-v1(featureX,featureY)`. Its centre
X is originX+256+`hash%129-64`; its level L is originY+192+`(hash>>8)%129`.
The complete 128x128 window starts at (centreX-64,L), with the query border
inside its feature cell. `groundwater-wetness-v1` at 2048x2048 sampled at
(centreX,L) must exceed -8192. L must be at least 96 below H in every column.
Classify the entire window through caves (ore replacement preserves its exact
Air/solid result). On the centre column, choose the bottommost Air as seed.
Traverse four-neighbour Air using neighbour order left, right, down, up.
Touching either side or the bottom rejects the entire candidate. A top-row
cell must have Air at y=L-1; volume must be >=32. Only the full accepted
component becomes water. The full result is recomputed identically for any
intersecting Chunk, and only that Chunk's intersection is written. Rejected
candidates do not carve, seal, truncate, or fill anything.

Water body summaries report complete bounds and volume, even for a partial
query. These are diagnostics, never infinite sources or runtime water authority.
For seeds 1/3/42 in [-4096,4096)x[256,3328), a local optimized probe found
10/8/11 lakes and 3/3/1 groundwater bodies respectively. Seed 2 produced 11
lakes and no groundwater in that finite range; no per-seed availability promise
is inferred from the thresholds.

## Working set

Normal generation keeps one output WorldChunk and no full-sized extra material
map. Scratch includes ChunkEdge columns (now 64 bytes including the v2 climate
fields, unused by v1), at most
`ceil(ChunkEdge/512)+1` small lake plans, 128 temporary columns, one 16384-byte
Air/visited classification array, and one 32768-byte uint16 queue. Groundwater
plans are processed serially and immediately written; accepted masks are not
accumulated. At edge 1024, all live arrays and plans are below 128KiB, and
WorkingBytes conservatively reserves 256KiB. Larger edges add conservative
per-column and lake-plan allowance. Flat needs no additional scratch. Bounded
pure-sampling diagnostics return their own explicitly allocated row-major
material vector; that vector is not used by GenerateChunk.
