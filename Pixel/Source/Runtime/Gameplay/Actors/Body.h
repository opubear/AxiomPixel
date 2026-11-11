#pragma once
#include "Runtime/Simulation/World/WorldMap.h"

namespace pixel {
struct BodyState {
    WorldPosition Position;
    float VerticalSpeed = 0; // cm/s
    bool Grounded = false;
};
class Body {
  public:
    static constexpr float Width = 100.f, Height = 200.f; // cm
    static constexpr float WalkSpeed = 800.f, JumpSpeed = 1600.f; // cm/s
    static constexpr float Gravity = 4500.f, MaximumFallSpeed = 4000.f; // cm/s^2, cm/s
    static constexpr float WidthCells = CentimetersToCells(Width), HeightCells = CentimetersToCells(Height);
    // Local coordinates are normalized after each tick, preserving sub-cell precision.
    CellCoord OriginChunk;
    float X = 0, Y = 0; // Chunk-local cm.
    bool Grounded = false;
    std::array<float, 2> Size{Width, Height}; // per-actor cm; player defaults stay unchanged
    WorldPosition GetWorldPosition() const { return {OriginChunk, {X, Y}}; }
    CellCoord CellOrigin() const { return {OriginChunk.X * storage::ChunkEdge, OriginChunk.Y * storage::ChunkEdge}; }
    void Tick(float duration, WorldMapView world, int horizontal, bool jump, float speed = WalkSpeed);
    void Fly(float duration, WorldMapView world, float velocityX, float velocityY);
    bool CanOccupy(WorldMapView world) const { return HasTerrain(world) && !Collides(world, X, Y); }
    bool HasTerrain(WorldMapView world) const;
    bool Occupies(int64_t x, int64_t y) const;
    BodyState CaptureState() const { return {GetWorldPosition(), VerticalSpeed, Grounded}; }
    void RestoreState(BodyState state);

  private:
    float VerticalSpeed = 0;
    bool Collides(WorldMapView world, float x, float y) const;
    float Submersion(WorldMapView world) const;
    void MoveAxis(WorldMapView world, float distance, bool vertical);
};
} // namespace pixel
