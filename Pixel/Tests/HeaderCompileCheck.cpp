#include "PixelGame.h"
#include "PixelRender/PixelRender.h"
#include "World/Environment.h"
#include "World/World.h"
#include "World/WindField.h"
#include <type_traits>

static_assert(std::is_base_of_v<IGameInterface, PixelGame>);
static_assert(!std::is_copy_constructible_v<pixel::WorldState>);
static_assert(!std::is_move_constructible_v<pixel::WorldState>);
