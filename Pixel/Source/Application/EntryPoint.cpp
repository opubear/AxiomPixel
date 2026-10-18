#include "ApplicationEntry.h"
#include "PixelApplication.h"

std::unique_ptr<Axiom> CreateApplication() {
    return std::make_unique<PixelApplication>();
}
