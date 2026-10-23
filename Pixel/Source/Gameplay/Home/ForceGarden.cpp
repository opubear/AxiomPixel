#include "Gameplay/GameSession.h"
#include "ForceGardenProgram.h"

namespace pixel {
void GameSession::InitializeForceGarden() {
    Garden.Enabled = true;
    Garden.SurfaceY = GetPlayer()->GetWorldPosition().Cell().Y + int64_t(Character::Height);
    const auto y = Garden.SurfaceY;
    // Authored content is installed once, before the first checkpoint. Loading
    // never replenishes these deposits or reconstructs modified terrain.
    GameWorld.FillRegion({464, y - 24, 96, 24}, {CellTypeId::Air});
    GameWorld.FillRegion({464, y, 96, 8}, {CellTypeId::GroundSoil});
    GameWorld.FillRegion({464, y, 96, 1}, {CellTypeId::GrassSoil});
    for (int64_t x = 516; x < 538; ++x) {
        const auto type = x < 520 ? CellTypeId::Stone : x < 522 ? CellTypeId::CopperOre : CellTypeId::CoalOre;
        GameWorld.SetCell(x, y + 2, {type});
    }
    // Preview the natural field while paused without advancing either bed.
    std::vector<FormulaEffectRecord> effects;
    StartGardenCrystal(effects);
}

GardenSample GameSession::SampleGarden(int64_t x, uint32_t growth) const {
    const CellCoord plant{x, Garden.SurfaceY - 1}, support{x, Garden.SurfaceY};
    const auto bounds = GameWorld.GetSimulationBounds();
    if (!bounds.Contains(plant) || !bounds.Contains(support)) return {};
    const auto space = GameWorld.TryGetCell(plant.X, plant.Y);
    const auto ground = GameWorld.TryGetCell(support.X, support.Y);
    if (!space || !ground) return {};
    GardenSample result;
    result.Sampled = true;
    result.HorizontalAcceleration = GameWorld.GetForce(plant.X, plant.Y).X;
    if (space->Type != CellTypeId::Air || !BlocksCharacter(ground->Type))
        result.Condition = GardenCondition::Obstructed;
    else if (result.HorizontalAcceleration < GardenMinimumForce)
        result.Condition = GardenCondition::NeedsForce;
    else if (result.HorizontalAcceleration > GardenMaximumForce)
        result.Condition = GardenCondition::ExcessForce;
    else if (growth == GardenMatureSteps)
        result.Condition = GardenCondition::Mature;
    else
        result.Condition = GardenCondition::Growing;
    return result;
}

void GameSession::StartGardenCrystal(std::vector<FormulaEffectRecord> &effects) {
    if (!Garden.Enabled || !Garden.CrystalEnabled
        || Formulas.HasCarrier(ProcessCarrierKind::NaturalSource, GardenCrystalSubject)) return;
    const auto y = Garden.SurfaceY - 4;
    const auto bounds = GameWorld.GetSimulationBounds();
    if (!bounds.Contains({GardenCrystalX - 8, y - 8}) || !bounds.Contains({GardenCrystalX + 8, y + 8})) return;
    const auto anchor = GameWorld.TryGetCell(GardenCrystalX, y);
    if (!anchor || anchor->Type != CellTypeId::Air) return;
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::NaturalSource;
    carrier.Subject = GardenCrystalSubject;
    carrier.Origin = WorldPosition::FromAbsolute(double(GardenCrystalX), double(y));
    carrier.Scope = {GardenCrystalX - 8, y - 8, 17, 17};
    carrier.MaximumRadius = carrier.ActivityRadius = 8;
    carrier.Credits = 1;
    carrier.Grants = formula::process::Mask(formula::process::Capability::Field);
    if (Formulas.CastProcess(GameWorld, ForceGardenProgram(), {4, 1}, carrier))
        effects.insert(effects.end(), Formulas.LastEffects().begin(), Formulas.LastEffects().end());
}

void GameSession::StepForceGarden(std::vector<FormulaEffectRecord> &effects) {
    if (!Garden.Enabled) return;
    StartGardenCrystal(effects);
    // Ecology reads the completed device/program phase. New sources can be
    // measured immediately; physical material motion starts next environment step.
    if (SampleGarden(GardenWildX, Garden.WildGrowth).Condition == GardenCondition::Growing) ++Garden.WildGrowth;
    if (SampleGarden(GardenHomeX, Garden.HomeGrowth).Condition == GardenCondition::Growing) ++Garden.HomeGrowth;
}
} // namespace pixel
