#include "Runtime/Gameplay/Application/PixelGame.h"
#include "Runtime/Render/PixelRender.h"
#include "Runtime/Simulation/World/Environment/Environment.h"
#include "Runtime/Simulation/World/World.h"
#include "Runtime/Simulation/World/Environment/Wind/WindField.h"
#include <type_traits>

static_assert(std::is_base_of_v<IGameInterface, PixelGame>);
static_assert(!std::is_copy_constructible_v<pixel::WorldMap>);
static_assert(!std::is_move_constructible_v<pixel::WorldMap>);
