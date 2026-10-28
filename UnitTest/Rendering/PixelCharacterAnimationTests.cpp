#include "Runtime/Render/CharacterAnimation.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template <class F> void Reject(F action) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error("invalid character configuration accepted");
}
std::vector<char> Read(std::string_view name) {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / name, std::ios::binary);
    if (!file) throw std::runtime_error("missing character fixture");
    return {std::istreambuf_iterator<char>(file), {}};
}
void Assets() {
    std::map<std::string, std::string> replacements;
    size_t reads = 0;
    pixel::ArtAssetStore store([&](std::string_view name) {
        ++reads;
        if (const auto found = replacements.find(std::string(name)); found != replacements.end())
            return std::vector<char>(found->second.begin(), found->second.end());
        return Read(name);
    });
    const auto art = store.LoadCharacter();
    Check(art->Layers.size() == 5 && store.CachedImageCount() == 5, "missing default outfit layers");
    Check(art->Layers.front()->Width == 800 && art->Layers.front()->Height == 448, "wrong atlas dimensions");
    const auto readCount = reads;
    Check(store.LoadCharacter("./CharacterAssets.json") == art && reads == readCount, "character cache reread files");
    const auto original = nlohmann::json::parse(Read("CharacterAssets.json"));
    replacements["alias.json"] = original.dump();
    Check(store.LoadCharacter("alias.json")->Layers == art->Layers, "duplicate decoded layers");
    auto reject = [&](auto change) {
        auto invalid = original;
        change(invalid["Character"]);
        replacements["retry.json"] = invalid.dump();
        Reject([&] { store.LoadCharacter("retry.json"); });
    };
    reject([](auto &j) { j["FrameSize"][0] = 0; });
    reject([](auto &j) { j["FrameSize"][0] = 81; });
    reject([](auto &j) { j["FootAnchor"][1] = 65; });
    reject([](auto &j) { j["PixelsPerCell"] = 0; });
    reject([](auto &j) { j["Layers"] = nlohmann::json::array(); });
    reject([](auto &j) { j["Layers"][0]["Path"] = "../ArtRaw/body.png"; });
    reject([](auto &j) { j["Animations"]["Run"]["FrameCount"] = 11; });
    reject([](auto &j) { j["Animations"]["Jump"]["Row"] = 7; });
    reject([](auto &j) { j["Animations"]["Fall"]["FramesPerSecond"] = -1; });
    replacements["retry.json"] = original.dump();
    Check(store.LoadCharacter("retry.json")->Layers == art->Layers, "corrected manifest could not be retried");
}
void Playback() {
    pixel::ArtAssetStore store(Read);
    const auto art = store.LoadCharacter();
    pixel::CharacterAnimation animation;
    pixel::ActorSnapshot actor{{1}, pixel::ActorKind::Player, {{-(int64_t(1) << 48), 0}, {10, 10}}, true};
    pixel::Camera2D camera;
    camera.SetWorldPosition(actor.Position);
    WorldGridFrame frame;
    frame.View.CellSizePixels = {21, 21};
    const auto publish = [&](double time, uint64_t world = 1) {
        BuildCharacterFrame(actor, camera, {800, 600}, frame);
        animation.Update(actor, world, time);
        animation.Apply(*art, frame);
        ValidateWorldGridFrame(frame.Read(), 1, 800, 600);
        return *frame.Character;
    };
    const auto idle = publish(0);
    Check(idle.FrameTexels == std::array<float, 4>{0, 0, 80, 64} && idle.Sprite[0] == 5, "idle did not use outfit atlas");
    Check(std::abs(idle.Bounds[1] + idle.Bounds[3] - 384) < .001, "sprite feet left collision bottom");
    Check(publish(.5).FrameTexels[0] == 240, "idle did not advance on simulation clock");
    Check(publish(.5).FrameTexels[0] == 240, "paused animation moved");
    actor.Position.Local[0] -= 1;
    const auto run = publish(.6);
    Check(animation.Motion() == pixel::CharacterMotion::Run && run.FrameTexels[1] == 128 && run.Sprite[1] == 1,
          "far-coordinate movement did not select mirrored run");
    Check(publish(.6).FrameTexels == run.FrameTexels, "repeat publication reset running animation");
    const auto stopped = publish(.7);
    Check(animation.Motion() == pixel::CharacterMotion::Idle && stopped.Sprite[1] == 1, "stopping lost facing");
    actor.Grounded = false;
    actor.Position.Local[1] = 9;
    Check(publish(.8).FrameTexels[1] == 192, "ascending did not select jump");
    actor.Position.Local[1] = 8;
    Check(publish(1.8).FrameTexels[0] == 240, "non-looping jump did not hold final pose");
    actor.Position.Local[1] = 9;
    Check(publish(1.9).FrameTexels[1] == 256, "descending did not select fall");
    actor.Grounded = true;
    Check(publish(2).FrameTexels[1] == 0, "landing did not return to idle");
    Check(publish(0, 2).Sprite[1] == 0, "new world retained old facing");
    frame.DisplayMode = GridDisplayMode::ForceField;
    BuildCharacterFrame(actor, camera, {800, 600}, frame);
    animation.Apply(*art, frame);
    Check(!frame.Character, "diagnostic view displayed character");
}
} // namespace
int main() {
    try {
        Assets(); Playback();
        std::cout << "Character assets and animation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
