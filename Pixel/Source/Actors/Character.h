#pragma once
#include "World/WorldMap.h"

namespace pixel {
struct CharacterState {
    WorldPosition Position;
    double VerticalSpeed = 0;
    bool Grounded = false;
};
class Character {
  public:
    static constexpr float Width = 2, Height = 4;
    // Local coordinates are normalized after each tick, preserving sub-cell precision.
    CellCoord OriginChunk;
    double X = 0, Y = 0;
    bool Grounded = false;
    WorldPosition GetWorldPosition() const { return {OriginChunk, {X, Y}}; }
    CellCoord CellOrigin() const { return {OriginChunk.X * storage::ChunkEdge, OriginChunk.Y * storage::ChunkEdge}; }
    void Tick(float duration, WorldMapView world, int horizontal, bool jump);
    bool Occupies(int64_t x, int64_t y) const;
    CharacterState CaptureState() const { return {GetWorldPosition(), VerticalSpeed, Grounded}; }
    void RestoreState(CharacterState state);

  private:
    double VerticalSpeed = 0;
    bool Collides(WorldMapView world, double x, double y) const;
    double Submersion(WorldMapView world) const;
    void MoveAxis(WorldMapView world, double distance, bool vertical);
};
} // namespace pixel
