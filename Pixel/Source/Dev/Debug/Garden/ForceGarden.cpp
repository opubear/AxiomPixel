#include "GardenScenario.h"
#include "ForceGardenProgram.h"

namespace pixel::debug {
static void StartGardenCrystal(World &world, FormulaBridge &formulas, const ForceGardenState &garden, std::vector<FormulaEffectRecord> &effects) {
    if (!garden.Enabled || !garden.CrystalEnabled
        || formulas.HasCarrier(ProcessCarrierKind::NaturalSource, GardenCrystalSubject)) return;
    const auto y = garden.SurfaceY - 4;
    const auto bounds = world.GetSimulationBounds();
    if (!bounds.Contains({GardenCrystalX - 8, y - 8}) || !bounds.Contains({GardenCrystalX + 8, y + 8})) return;
    const auto anchor = world.TryGetCell(GardenCrystalX, y);
    if (!anchor || anchor->Type != CellTypeId::Air) return;
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::NaturalSource;
    carrier.Subject = GardenCrystalSubject;
    carrier.Origin = WorldPosition::FromAbsolute(double(GardenCrystalX), double(y));
    carrier.Scope = {GardenCrystalX - 8, y - 8, 17, 17};
    carrier.MaximumRadius = carrier.ActivityRadius = 8;
    carrier.Credits = 1;
    carrier.Grants = formula::process::Mask(formula::process::Capability::Field);
    if (formulas.CastProcess(world, ForceGardenProgram(), {4, 1}, carrier))
        effects.insert(effects.end(), formulas.LastEffects().begin(), formulas.LastEffects().end());
}

void InitializeForceGarden(World &world, FormulaBridge &formulas, ForceGardenState &garden, const Body &player) {
    garden.Enabled = true;
    garden.SurfaceY = player.GetWorldPosition().Cell().Y + int64_t(Body::Height);
    const auto y = garden.SurfaceY;
    // Authored content is installed once, before the first checkpoint. Loading
    // never replenishes these deposits or reconstructs modified terrain.
    world.FillRegion({464, y - 24, 96, 24}, {CellTypeId::Air});
    world.FillRegion({464, y, 96, 8}, {CellTypeId::GroundSoil});
    world.FillRegion({464, y, 96, 1}, {CellTypeId::GrassSoil});
    for (int64_t x = 516; x < 538; ++x) {
        const auto type = x < 520 ? CellTypeId::Stone : x < 522 ? CellTypeId::CopperOre : CellTypeId::CoalOre;
        world.SetCell(x, y + 2, {type});
    }
    // Preview the natural field while paused without advancing either bed.
    std::vector<FormulaEffectRecord> effects;
    StartGardenCrystal(world, formulas, garden, effects);
}

GardenSample SampleGarden(const World &world, const ForceGardenState &garden, int64_t x, uint32_t growth) {
    const CellCoord plant{x, garden.SurfaceY - 1}, support{x, garden.SurfaceY};
    const auto bounds = world.GetSimulationBounds();
    if (!bounds.Contains(plant) || !bounds.Contains(support)) return {};
    const auto space = world.TryGetCell(plant.X, plant.Y);
    const auto ground = world.TryGetCell(support.X, support.Y);
    if (!space || !ground) return {};
    GardenSample result;
    result.Sampled = true;
    result.HorizontalAcceleration = world.GetForce(plant.X, plant.Y).X;
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

void StepForceGarden(World &world, FormulaBridge &formulas, ForceGardenState &garden, std::vector<FormulaEffectRecord> &effects) {
    if (!garden.Enabled) return;
    StartGardenCrystal(world, formulas, garden, effects);
    // Ecology reads the completed device/program phase. New sources can be
    // measured immediately; physical material motion starts next environment step.
    if (SampleGarden(world, garden, GardenWildX, garden.WildGrowth).Condition == GardenCondition::Growing) ++garden.WildGrowth;
    if (SampleGarden(world, garden, GardenHomeX, garden.HomeGrowth).Condition == GardenCondition::Growing) ++garden.HomeGrowth;
}
} // namespace pixel::debug
