#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {
using namespace pixel;
static_assert(std::is_same_v<decltype(Body::X), float>);
static_assert(std::is_same_v<decltype(BodyState::VerticalSpeed), float>);
static_assert(std::is_same_v<decltype(WorldPosition::Local), std::array<float, 2>>);
static_assert(std::is_same_v<Camera2D::Position, std::array<float, 2>>);
static_assert(CentimetersPerCell == 50 && Body::Height == 200 && Body::Width == 100);

void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void Coordinates() {
    const auto point = WorldPosition::FromAbsolute(125, -25);
    Check(point.Cell() == CellCoord{2, -1} && point.RelativeTo({2, -1}) == std::array<float, 2>{25, 25},
          "centimetres did not map to the correct signed cell and offset");
    const int64_t far = int64_t{1} << 45;
    WorldPosition crossing{{far, -far}, {WorldPosition::ChunkCentimeters - .25f, -.5f}};
    crossing.Normalize();
    Check(crossing.Chunk == CellCoord{far, -far - 1}
              && crossing.Local[1] == WorldPosition::ChunkCentimeters - .5f,
          "negative centimetre offset normalized into the wrong chunk");
    crossing.Local[0] += .5f;
    crossing.Normalize();
    Check(crossing.Chunk.X == far + 1 && crossing.Local[0] == .25f,
          "sub-centimetre motion was lost at a distant chunk boundary");
    auto tinyNegative = WorldPosition::FromAbsolute(-std::numeric_limits<float>::epsilon(), 0);
    Check(tinyNegative.Cell().X == -1 && tinyNegative.Local[0] < WorldPosition::ChunkCentimeters,
          "negative roundoff crossed to the positive side of a grid boundary");
}

void WalkingAcrossChunks() {
    for (const int64_t chunkX : {int64_t{0}, int64_t{1} << 45, -(int64_t{1} << 45)}) {
        WorldMap map;
        for (int offset = 0; offset < 2; ++offset) {
            auto chunk = std::make_unique<WorldChunk>(CellCoord{chunkX + offset, 0});
            chunk->Initialize(std::nullopt);
            map.Insert(std::move(chunk));
        }
        const int64_t boundary = (chunkX + 1) * storage::ChunkEdge;
        map.Fill({boundary - 20, 40, 64, 4}, {CellTypeId::Dirt, {}});
        Actors actors;
        actors.Spawn(ActorKind::Player, WorldPosition::FromCell({boundary - 8, 36}));
        const CellCoord reference{boundary - 8, 36};
        for (int step = 0; step < 60; ++step)
            actors.Step(map.View({boundary - 20, 0, 64, 64}), {1, false});
        const auto moved = actors.Player()->GetWorldPosition().RelativeTo(reference);
        Check(std::abs(moved[0] - 800) < .25f && std::abs(moved[1]) < .01f && actors.Player()->Grounded,
              "float centimetre walking changed speed or ground collision across chunks");
        Check(actors.Player()->GetWorldPosition().Chunk.X == chunkX + 1, "player never crossed the chunk");
    }
}

void SessionAndPresentation() {
    for (const int frames : {30, 60, 144}) {
        GameSession session;
        session.Control(NativeCaller::Driver, api::SetPaused{false});
        session.EnableSimulationThread();
        const auto start = session.GetPlayer()->GetWorldPosition();
        const auto reference = start.Cell();
        for (int frame = 0; frame < frames; ++frame) {
            auto presentation = session.CapturePlayerPresentation();
            session.RunFrame(1.f / frames, {1, false}, [&] { presentation.Advance(1.f / frames, {1, false}); });
            Check(presentation.Player->Position.Local == session.GetPlayer()->GetWorldPosition().Local,
                  "presented and authoritative centimetre positions disagree");
        }
        const auto moved = session.GetPlayer()->GetWorldPosition().RelativeTo(reference);
        Check(session.CurrentCommit().Tick == 60 && std::abs(moved[0] - 800) < .25f,
              "one simulation second did not move approximately 800 centimetres");
        std::cout << frames << " fps: " << moved[0] << " cm in 60 ticks\n";
    }
}

void LegacyCheckpoint() {
    // Independent v1 cell-unit fixture: actor at (10.125,36), jumping at -32 cells/s.
    const auto legacy = DecodeGameplayState(
        "PixelGameplay 1\n0 1 1 1 0\n2 1\n1 0 0 0 10.125 36 -32 1\n"
        "2 1 0\n1 0\n1 0\n\"legacy program fixture\"\n");
    const auto &actor = legacy.Actors.Records.front().Body;
    Check(actor.X == 506.25f && actor.Y == 1800 && actor.CaptureState().VerticalSpeed == -1600,
          "legacy position or velocity was interpreted as centimetres");
    const auto restored = DecodeGameplayState(EncodeGameplayState(legacy));
    const auto &copy = restored.Actors.Records.front().Body;
    Check(copy.X == actor.X && copy.Y == actor.Y && copy.CaptureState().VerticalSpeed == -1600,
          "centimetre state changed during a legacy-format checkpoint round trip");
}
} // namespace

int main() {
    try {
        Coordinates();
        WalkingAcrossChunks();
        SessionAndPresentation();
        LegacyCheckpoint();
        std::cout << "Float centimetre coordinates, motion and compatibility passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
