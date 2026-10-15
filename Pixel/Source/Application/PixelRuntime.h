#pragma once
#include "Render/RenderSetup.h"

class PixelGame;
// Binds Pixel's render policy and UI overlay; publishing stops before game teardown.
RenderSetup CreatePixelRenderSetup(PixelGame &game);
