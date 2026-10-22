#include "Gameplay/GameSession.h"
#include "LLMView/JsonAdapter.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
using Json = nlohmann::json;

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

llm::QueryPage Query(llm::ViewService &views, llm::ObserverId observer, llm::ViewId view,
                     api::Topic topic, api::Rect scope, uint32_t resolution = 1, llm::CursorId cursor = 0) {
    const auto prepared = views.Query(observer, view, {topic, scope, resolution}, cursor);
    Require(prepared.Result == llm::Status::Ok, "demo query preparation failed");
    auto response = views.Deliver(observer, prepared.Delivery);
    Require(response.Result == llm::Status::Ok && response.Page.has_value(), "demo query delivery failed");
    return std::move(*response.Page);
}

Json EncodePage(llm::QueryPage page) {
    llm::Response response;
    response.Page = std::move(page);
    return llm::ToJson(response).at("page");
}

const llm::CellSample &Find(const llm::QueryPage &page, api::Cell target) {
    for (const auto &cell : page.Cells) if (cell.Location == target) return cell;
    throw std::runtime_error("mined cell absent from page");
}
} // namespace

int main() {
    GameSession session;
    llm::ViewService views(session);
    llm::JsonAdapter adapter(views);
    const auto observer = views.RegisterObserver(llm::Policy::FullAccess());
    const api::Rect scope{{504, 504}, 32, 16};
    const api::Cell target{520, 512};
    views.AdvanceServiceFrame(); // Explicit host iteration; the session is paused.
    const auto before = views.OpenView(observer, scope);
    Require(before.View.has_value(), "before view was unavailable");
    const auto first = Query(views, observer, before.View->Id, api::Topic::Materials, scope);

    Json transcript;
    transcript["capabilities"] = Json::parse(adapter.Handle(observer, R"({"method":"DescribeCapabilities"})"));
    transcript["before"]["view"] = llm::ToJson(before).at("view");
    for (const auto topic : {api::Topic::AirVelocity, api::Topic::Player, api::Topic::Programs, api::Topic::Runtime})
        transcript["before"][llm::TopicName(topic)] = EncodePage(Query(views, observer, before.View->Id,
            topic, scope, topic == api::Topic::AirVelocity ? 16 : 1));

    const auto receipt = session.Execute(NativeCaller::Driver, api::MineRegion{target, 10});
    Require(receipt.Applied() && receipt.Facts.Count == 1, "offline mining did not commit one fact");
    transcript["receipt"] = llm::ToJson(receipt);
    views.AdvanceServiceFrame();
    const auto frozenSecond = Query(views, observer, before.View->Id, api::Topic::Materials, scope, 1, first.NextCursor);
    Require(Find(frozenSecond, target).MaterialId == 10, "old page observed the new material");
    transcript["before"]["materialAtTarget"] = *Find(frozenSecond, target).MaterialId;

    const auto after = views.OpenView(observer, scope);
    Require(after.View.has_value(), "after view was unavailable");
    const auto material = Query(views, observer, after.View->Id, api::Topic::Materials, {target, 1, 1});
    Require(Find(material, target).MaterialId == 0, "new view did not observe the mined air cell");
    transcript["after"]["materials"] = EncodePage(material);
    transcript["after"]["air_velocity"] = EncodePage(Query(views, observer, after.View->Id,
        api::Topic::AirVelocity, scope, 16));

    auto pendingFacts = views.ReadFacts(observer, after.View->Id, *before.View->FactHighWatermark);
    auto facts = views.Deliver(observer, pendingFacts.Delivery);
    Require(facts.Facts && facts.Facts->Facts.size() == 1
        && std::get<api::RequestKey>(facts.Facts->Facts[0].Cause) == receipt.Key
        && facts.Facts->Facts[0].CommittedAt == receipt.ResolvedAt, "receipt/fact cause or commit mismatch");
    transcript["facts"] = llm::ToJson(facts);
    transcript["unsupportedItems"] = EncodePage(Query(views, observer, after.View->Id, api::Topic::Items, scope));
    transcript["checks"] = {"old pagination remains frozen", "new view observes mining",
        "receipt and fact share cause and commit", "no model or network required"};
    const auto metrics = views.Statistics();
    transcript["retainedBytes"] = std::to_string(metrics.RetainedBytes);
    Require(views.CloseView(observer, after.View->Id) == llm::Status::Ok, "close failed");
    std::cout << transcript.dump(2) << '\n';
}
