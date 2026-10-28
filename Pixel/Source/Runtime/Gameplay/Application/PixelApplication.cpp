#include "PixelApplication.h"
#include "PixelGame.h"

bool PixelApplication::OnCloseRequested() {
    auto &session = static_cast<PixelGame &>(*GetGame()).GetSession();
    return !session.IsPersistent()
        || session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded();
}

std::unique_ptr<IGameInterface> PixelApplication::CreateGame() {
    return std::make_unique<PixelGame>();
}
