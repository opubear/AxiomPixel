#pragma once
#include "Render/RenderSetup.h"

class PixelGame;
// Selects the 2D policy; the application supplies UI through the engine overlay callback.
RenderSetup CreatePixelRenderSetup(PixelGame &game);
