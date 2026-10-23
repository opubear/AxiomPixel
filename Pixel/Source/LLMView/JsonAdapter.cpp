#include "JsonAdapter.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace pixel::llm {
namespace {
using Json = nlohmann::json;
template<class... Visitors> struct Overload : Visitors... { using Visitors::operator()...; };

const char *AvailabilityName(api::Availability value) {
    switch (value) {
    case api::Availability::Available: return "available";
    case api::Availability::Partial: return "partial";
    case api::Availability::NotApplicable: return "not_applicable";
    case api::Availability::Unknown: return "unknown";
    case api::Availability::Unsupported: return "unsupported";
    }
    throw std::logic_error("unknown observation availability");
}

const char *WindName(api::WindKind value) {
    switch (value) {
    case api::WindKind::Coarse: return "coarse";
    case api::WindKind::Reference: return "reference";
    case api::WindKind::Procedural: return "procedural";
    case api::WindKind::Hybrid: return "hybrid";
    case api::WindKind::Adaptive: return "adaptive";
    case api::WindKind::Lbm: return "lbm";
    case api::WindKind::Empty: return "empty";
    }
    throw std::logic_error("unknown wind backend");
}

const char *AirName(api::AirSampleKind value) {
    switch (value) {
    case api::AirSampleKind::NotResident: return "not_resident";
    case api::AirSampleKind::NotApplicable: return "not_applicable";
    case api::AirSampleKind::Sampled: return "sampled";
    case api::AirSampleKind::OutsideDomain: return "outside_domain";
    case api::AirSampleKind::EmptyBackend: return "empty_backend";
    case api::AirSampleKind::Disabled: return "disabled";
    }
    throw std::logic_error("unknown air sample kind");
}

const char *FactStatusName(api::FactReadStatus value) {
    switch (value) {
    case api::FactReadStatus::Available: return "Available";
    case api::FactReadStatus::Gap: return "Gap";
    case api::FactReadStatus::StaleEpoch: return "StaleEpoch";
    case api::FactReadStatus::InvalidRange: return "InvalidRange";
    case api::FactReadStatus::LimitExceeded: return "LimitExceeded";
    }
    throw std::logic_error("unknown fact read status");
}

Json CellJson(api::Cell cell) {
    return {{"x", std::to_string(cell.X)}, {"y", std::to_string(cell.Y)}};
}

Json RectJson(const api::Rect &rect) {
    return {{"origin", CellJson(rect.Origin)}, {"width", rect.Width}, {"height", rect.Height}};
}

Json PositionJson(const api::Position &position) {
    return {{"anchorCell", CellJson(position.AnchorCell)}, {"offset", position.Offset}};
}

Json StampJson(const api::CommitStamp &stamp) {
    return {{"worldId", stamp.WorldId}, {"worldEpoch", std::to_string(stamp.WorldEpoch)},
            {"tick", std::to_string(stamp.Tick)}, {"revision", std::to_string(stamp.Revision)}};
}

Json ProgramJson(const api::ProgramRef &program) {
    return {{"resourceId", std::to_string(program.ResourceId)}, {"version", std::to_string(program.Version)}};
}

Json RequestKeyJson(const api::RequestKey &key) {
    return {{"worldEpoch", std::to_string(key.WorldEpoch)}, {"caller", std::to_string(key.Caller)},
            {"sequence", std::to_string(key.Sequence)}};
}

Json FactIdJson(const api::FactId &id) {
    return {{"worldEpoch", std::to_string(id.WorldEpoch)}, {"sequence", std::to_string(id.Sequence)}};
}

Json InstanceJson(const api::InstanceRef &instance) {
    return {{"worldEpoch", std::to_string(instance.WorldEpoch)}, {"instanceId", std::to_string(instance.InstanceId)}};
}

Json FactCauseJson(const api::FactCause &cause) {
    return std::visit(Overload{
        [](const api::RequestKey &key) -> Json {
            return {{"kind", "request"}, {"request", RequestKeyJson(key)}};
        },
        [](const api::AuthorityCause &authority) -> Json {
            return {{"kind", "authority"}, {"version", std::to_string(authority.Version)}};
        },
        [](const api::DeviceCause &device) -> Json {
            return {{"kind", "device"}, {"deviceId", std::to_string(device.DeviceId)},
                    {"instanceId", std::to_string(device.InstanceId)}};
        },
        [](const api::ProgramEffectCause &effect) -> Json {
            Json result{{"kind", "program_effect"}, {"instance", InstanceJson(effect.Instance)},
                        {"program", ProgramJson(effect.Program)},
                        {"emissionSequence", std::to_string(effect.EmissionSequence)}};
            if (effect.ActivationRequest) result["activationRequest"] = RequestKeyJson(*effect.ActivationRequest);
            if (effect.ParentFact) result["parentFact"] = FactIdJson(*effect.ParentFact);
            return result;
        }
    }, cause);
}

Json WindConfigurationJson(const api::WindConfiguration &configuration) {
    return {{"backendKind", WindName(configuration.Kind)}, {"boundaryY", std::to_string(configuration.BoundaryY)}};
}

Json FactPayloadJson(const api::FactPayload &payload) {
    return std::visit(Overload{
        [](const api::AuthorityChanged &value) -> Json {
            return {{"kind", "AuthorityChanged"}, {"version", std::to_string(value.Version)},
                {"deviceId", std::to_string(value.DeviceId)}, {"region", RectJson(value.Region)},
                {"protected", value.Protected}, {"observationAllowed", value.ObservationAllowed},
                {"interventionAllowed", value.InterventionAllowed}};
        },
        [](const api::InterventionStopped &value) -> Json {
            return {{"kind", "InterventionStopped"}, {"instance", InstanceJson(value.Instance)},
                {"subject", std::to_string(value.Subject)}, {"reason", value.Reason == api::InterventionStopReason::ControlRevoked
                    ? "control_revoked" : "lease_expired"}};
        },
        [](const api::ForceObserved &value) -> Json {
            return {{"kind", "ForceObserved"}, {"actorId", std::to_string(value.ActorId)},
                    {"location", CellJson(value.Location)},
                    {"accelerationCellsPerSecondSquared", value.AccelerationCellsPerSecondSquared}};
        },
        [](const api::FieldSourceChanged &value) -> Json {
            const char *event = value.Event == api::FieldSourceEvent::Installed ? "installed"
                : value.Event == api::FieldSourceEvent::Updated ? "updated" : "stopped";
            return {{"kind", "FieldSourceChanged"}, {"handle", std::to_string(value.Handle)}, {"event", event}};
        },
        [](const api::DeviceChanged &value) -> Json {
            const auto &d = value.Device;
            const auto event = [&] {
                switch (value.Event) {
                case api::DeviceEvent::Placed: return "placed";
                case api::DeviceEvent::Removed: return "removed";
                case api::DeviceEvent::Supplied: return "supplied";
                case api::DeviceEvent::Enabled: return "enabled";
                case api::DeviceEvent::StateChanged: return "state_changed";
                }
                throw std::logic_error("unknown device event");
            }();
            return {{"kind", "DeviceChanged"}, {"event", event}, {"deviceId", std::to_string(d.Ref.Id)},
                    {"version", std::to_string(d.Ref.Version)}, {"typeId", ForceEmitterType},
                    {"owner", std::to_string(d.Owner)}, {"target", CellJson({d.X, d.Y})},
                    {"enabled", d.Enabled}, {"state", DeviceStateName(d.State)}, {"fuel", d.Fuel},
                    {"instanceId", std::to_string(d.Instance)}, {"cycles", std::to_string(d.Cycles)}};
        },
        [](const api::MaterialRemoved &value) -> Json {
            return {{"kind", "MaterialRemoved"}, {"target", CellJson(value.Target)},
                    {"oldMaterial", value.OldMaterial}, {"newMaterial", value.NewMaterial}};
        },
        [](const api::MaterialPlaced &value) -> Json {
            return {{"kind", "MaterialPlaced"}, {"target", CellJson(value.Target)},
                    {"oldMaterial", value.OldMaterial}, {"newMaterial", value.NewMaterial}};
        },
        [](const api::ProgramActivated &value) -> Json {
            return {{"kind", "ProgramActivated"}, {"program", ProgramJson(value.Program)},
                    {"activationId", std::to_string(value.ActivationId)}};
        },
        [](const api::WindConfigured &value) -> Json {
            return {{"kind", "WindConfigured"}, {"before", WindConfigurationJson(value.Before)},
                    {"after", WindConfigurationJson(value.After)}};
        },
        [](const api::WindPulseSubmitted &value) -> Json {
            return {{"kind", "WindPulseSubmitted"}, {"center", PositionJson(value.Center)},
                    {"radiusCells", value.RadiusCells}, {"instance", InstanceJson(value.Instance)},
                    {"requestedDeltaVelocityCellsPerSecond", value.RequestedDeltaVelocityCellsPerSecond}};
        },
        [](const api::MaterialRemovedInDisk &value) -> Json {
            return {{"kind", "MaterialRemovedInDisk"}, {"center", PositionJson(value.Center)},
                    {"radiusCells", value.RadiusCells}, {"changed", value.Changed},
                    {"blocked", value.Blocked}, {"notReady", value.NotReady}};
        }
    }, payload);
}

Json FactsJson(const api::FactReadResult &facts) {
    Json result{{"status", FactStatusName(facts.Status)}, {"worldEpoch", std::to_string(facts.WorldEpoch)},
                {"highWatermark", std::to_string(facts.HighWatermark)},
                {"firstAvailableSequence", std::to_string(facts.FirstAvailableSequence)},
                {"nextSequence", std::to_string(facts.NextSequence)}, {"hasMore", facts.HasMore},
                {"facts", Json::array()}};
    for (const auto &fact : facts.Facts)
        result["facts"].push_back({{"id", FactIdJson(fact.Id)}, {"committedAt", StampJson(fact.CommittedAt)},
                                  {"cause", FactCauseJson(fact.Cause)}, {"payload", FactPayloadJson(fact.Payload)}});
    return result;
}

Json HistoryRecordJson(const knowledge::HistoryRecord &record) {
    const auto &fact = record.Source;
    return {{"id", {{"worldId", fact.CommittedAt.WorldId}, {"sequence", std::to_string(record.Sequence)}}},
            {"sourceFactId", FactIdJson(fact.Id)}, {"committedAt", StampJson(fact.CommittedAt)},
            {"cause", FactCauseJson(fact.Cause)}, {"payload", FactPayloadJson(fact.Payload)}};
}

Json HistoryJson(const knowledge::HistoryRead &history) {
    Json result{{"status", history.Status == knowledge::ReadStatus::Gap ? "Gap" : "Available"},
                {"firstSequence", std::to_string(history.FirstSequence)},
                {"highWatermark", std::to_string(history.HighWatermark)},
                {"nextSequence", std::to_string(history.NextSequence)}, {"hasMore", history.HasMore},
                {"records", Json::array()}};
    for (const auto &record : history.Records) result["records"].push_back(HistoryRecordJson(record));
    return result;
}

Json DiscoveryJson(const knowledge::Discovery &discovery, const std::string &worldId) {
    Json result{{"ownerActor", std::to_string(discovery.OwnerActor)}, {"location", CellJson(discovery.Location)},
                {"observed", discovery.Observed}, {"analyzed", discovery.Analyzed},
                {"canCopy", discovery.CanCopy}, {"canEdit", discovery.CanEdit}, {"evidence", nullptr},
                {"evidenceAvailability", discovery.EvidenceSequence ? "available" : "permission_denied"}};
    if (discovery.EvidenceSequence)
        result["evidence"] = {{"worldId", worldId}, {"sequence", std::to_string(discovery.EvidenceSequence)}};
    return result;
}

Json ViewJson(const ViewDescriptor &view) {
    Json result{{"schema", "pixel.observation/1"}, {"schemaVersion", view.SchemaVersion},
                {"viewId", std::to_string(view.Id)}, {"commitStamp", StampJson(view.Stamp)},
                {"scope", RectJson(view.Scope)}, {"expiresAtServiceFrame", std::to_string(view.ExpiresAtServiceFrame)}};
    if (view.FactHighWatermark) result["factHighWatermark"] = std::to_string(*view.FactHighWatermark);
    return result;
}

Json WindJson(const api::WindSampling &wind) {
    Json result{{"contractVersion", wind.ContractVersion}, {"backendKind", WindName(wind.Backend)},
                {"enabled", wind.Enabled}, {"domain", RectJson(wind.Domain)},
                {"localObstacleCorrection", wind.LocalObstacleCorrection}};
    if (wind.SolverSpacingCells) result["solverSpacingCells"] = *wind.SolverSpacingCells;
    if (wind.AdaptiveMaximumEdgeCells) result["adaptiveMaximumEdgeCells"] = *wind.AdaptiveMaximumEdgeCells;
    return result;
}

Json WindSummaryJson(const api::WindSummary &summary) {
    Json result{{"scope", RectJson(summary.Scope)}, {"requestedCellCount", summary.RequestedCells},
                {"residentCellCount", summary.ResidentCells}, {"airSampleCount", summary.AirSamples},
                {"nonAirCount", summary.NonAirCells}, {"unknownCellCount", summary.UnknownCells},
                {"outsideDomainSampleCount", summary.OutsideDomainSamples},
                {"emptyBackendSampleCount", summary.EmptyBackendSamples}, {"disabledSampleCount", summary.DisabledSamples},
                {"oldestSampledAtRevision", std::to_string(summary.OldestSampledAtRevision)},
                {"newestSampledAtRevision", std::to_string(summary.NewestSampledAtRevision)},
                {"availability", AvailabilityName(summary.Status)}, {"coverageFraction", summary.CoverageFraction()}};
    if (summary.MeanVelocityCellsPerSecond) result["meanVelocityCellsPerSecond"] = *summary.MeanVelocityCellsPerSecond;
    return result;
}

Json PlayerJson(const api::PlayerObservation &player) {
    return {{"actorId", std::to_string(player.ActorId)}, {"location", PositionJson(player.Location)}, {"sizeCells", player.SizeCells}, {"grounded", player.Grounded}};
}

Json ProgramsJson(const api::ProgramObservation &programs) {
    return {{"applied", ProgramJson(programs.Applied)}, {"runningRoots", programs.RunningRoots},
            {"projectiles", programs.Projectiles}, {"pending", programs.Pending}};
}

const char *CarrierName(api::CarrierKind carrier) {
    switch (carrier) {
    case api::CarrierKind::Player: return "player";
    case api::CarrierKind::NaturalSource: return "natural_source";
    case api::CarrierKind::FixedFixture: return "fixed_fixture";
    case api::CarrierKind::Device: return "device";
    case api::CarrierKind::Intervention: return "intervention";
    }
    throw std::logic_error("unknown process carrier");
}

const char *PhaseName(api::ProcessPhase phase) {
    switch (phase) {
    case api::ProcessPhase::Ready: return "ready";
    case api::ProcessPhase::Sleeping: return "sleeping";
    case api::ProcessPhase::Awaiting: return "awaiting";
    case api::ProcessPhase::Ended: return "ended";
    }
    throw std::logic_error("unknown process phase");
}

Json InventoryJson(const api::InventoryObservation &inventory) {
    Json result{{"containerId", std::to_string(inventory.ContainerId)}, {"version", std::to_string(inventory.Version)},
                {"ownerActor", std::to_string(inventory.OwnerActor)}, {"deviceId", std::to_string(inventory.DeviceId)},
                {"location", PositionJson(inventory.Location)}, {"capacity", inventory.Capacity},
                {"quantity", inventory.Quantity}, {"stacks", Json::array()}};
    for (const auto &stack : inventory.Stacks)
        result["stacks"].push_back({{"itemId", stack.ItemId}, {"definitionVersion", stack.DefinitionVersion},
                                    {"quantity", stack.Quantity}, {"reserved", stack.Reserved}});
    return result;
}

Json DeviceJson(const api::DeviceObservation &device) {
    Json result{{"deviceId", std::to_string(device.Ref.Id)}, {"version", std::to_string(device.Ref.Version)},
                {"typeId", device.TypeId}, {"ownerActor", std::to_string(device.OwnerActor)},
                {"anchor", CellJson(device.Anchor)}, {"enabled", device.Enabled},
                {"state", DeviceStateName(device.State)}, {"reason", api::DeviceStateReason(device.State)},
                {"instanceId", std::to_string(device.InstanceId)}, {"cycles", std::to_string(device.Cycles)}};
    if (device.Fuel) result["fuel"] = *device.Fuel;
    return result;
}

Json ProcessJson(const api::ProcessObservation &process) {
    return {{"instanceId", std::to_string(process.InstanceId)}, {"subject", std::to_string(process.Subject)},
            {"program", ProgramJson(process.Program)}, {"carrier", CarrierName(process.Carrier)},
            {"location", PositionJson(process.Location)}, {"phase", PhaseName(process.Phase)},
            {"suspended", process.Suspended}, {"activeSteps", std::to_string(process.ActiveSteps)},
            {"remainingSteps", std::to_string(process.RemainingSteps)}, {"waitingFor", process.WaitingFor}};
}

Json FieldSourceJson(const api::FieldSourceObservation &source) {
    return {{"handle", std::to_string(source.Handle)}, {"ownerId", std::to_string(source.OwnerId)},
            {"ownerInstance", std::to_string(source.OwnerInstance)}, {"deviceId", std::to_string(source.DeviceId)},
            {"center", PositionJson(source.Center)}, {"radiusCells", source.RadiusCells},
            {"accelerationCellsPerSecondSquared", source.AccelerationCellsPerSecondSquared},
            {"remainingSteps", std::to_string(source.RemainingSteps)}, {"suspended", source.Suspended},
            {"contributing", source.Contributing}};
}

Json ProjectileJson(const api::ProjectileObservation &projectile) {
    return {{"instanceId", std::to_string(projectile.InstanceId)}, {"projectileId", std::to_string(projectile.ProjectileId)},
            {"program", ProgramJson(projectile.Program)}, {"location", PositionJson(projectile.Location)},
            {"velocityCellsPerSecond", projectile.VelocityCellsPerSecond}, {"radiusCells", projectile.RadiusCells}};
}

std::string_view SourceFor(api::Topic topic) {
    switch (topic) {
    case api::Topic::Materials: return api::ObservationSnapshot::MaterialSource;
    case api::Topic::AirVelocity: return api::ObservationSnapshot::WindSource;
    case api::Topic::Player: case api::Topic::Programs: case api::Topic::Runtime:
        return api::ObservationSnapshot::ObjectSource;
    case api::Topic::Items: return "session.inventory";
    case api::Topic::Devices: return "session.devices";
    case api::Topic::FieldSources: return "environment.field_sources";
    case api::Topic::Knowledge: return "knowledge.discoveries";
    case api::Topic::History: return "knowledge.important_history";
    default: return {};
    }
}

std::string_view TopicUnit(api::Topic topic) {
    switch (topic) {
    case api::Topic::AirVelocity: return "cell/second";
    case api::Topic::Materials: return "material_id";
    case api::Topic::Player: return "cell";
    case api::Topic::Items: return "item";
    case api::Topic::Devices: return "device";
    case api::Topic::FieldSources: return "cell/second^2";
    case api::Topic::Knowledge: return "discovery";
    case api::Topic::History: return "history_record";
    default: return "count";
    }
}

Json PageJson(const QueryPage &page) {
    Json result = ViewJson(page.View);
    result["viewScope"] = RectJson(page.View.Scope);
    result["scope"] = RectJson(page.Query.Scope);
    result["topic"] = TopicName(page.Query.Topic);
    result["schemaVersion"] = page.Query.SchemaVersion;
    result["resolutionCells"] = page.Query.ResolutionCells;
    result["pageSize"] = page.Query.PageSize;
    result["unit"] = page.Unit;
    result["availability"] = AvailabilityName(page.Availability);
    result["reason"] = page.Reason;
    result["nextCursor"] = std::to_string(page.NextCursor);
    if (page.Availability == api::Availability::Unsupported) return result;
    result["requestedCellCount"] = page.RequestedCells;
    result["residentCellCount"] = page.ResidentCells;
    result["coverageFraction"] = page.RequestedCells ? double(page.ResidentCells) / page.RequestedCells : 0;
    result["oldestSampledAtRevision"] = std::to_string(page.OldestSampledAtRevision);
    result["newestSampledAtRevision"] = std::to_string(page.NewestSampledAtRevision);
    if (const auto source = SourceFor(page.Query.Topic); !source.empty()) result["source"] = source;

    if (page.Query.Topic == api::Topic::Materials || page.Query.Topic == api::Topic::AirVelocity) {
        result["cells"] = Json::array();
        for (const auto &cell : page.Cells) {
            Json value{{"location", CellJson(cell.Location)}, {"sampledAtRevision", std::to_string(cell.SampledAtRevision)}};
            if (cell.MaterialId) value["materialId"] = *cell.MaterialId;
            if (page.Query.Topic == api::Topic::AirVelocity) {
                value["airKind"] = AirName(cell.AirKind);
                if (cell.AirVelocity) value["velocityCellsPerSecond"] = *cell.AirVelocity;
            }
            result["cells"].push_back(std::move(value));
        }
    }
    if (page.Wind) result["wind"] = WindJson(*page.Wind);
    if (page.Query.Topic == api::Topic::AirVelocity && page.Query.ResolutionCells == 16) {
        result["windSummaries"] = Json::array();
        for (const auto &summary : page.WindSummaries) result["windSummaries"].push_back(WindSummaryJson(summary));
    }
    if (page.Player) result["player"] = PlayerJson(*page.Player);
    if (page.Programs) result["programs"] = ProgramsJson(*page.Programs);
    if (page.Paused) result["paused"] = *page.Paused;
    if (page.Query.Topic == api::Topic::Items) {
        result["totalRecords"] = page.TotalRecords;
        result["inventories"] = Json::array();
        for (const auto &inventory : page.Inventories) result["inventories"].push_back(InventoryJson(inventory));
    }
    if (page.Query.Topic == api::Topic::Devices) {
        result["totalRecords"] = page.TotalRecords;
        result["devices"] = Json::array();
        for (const auto &device : page.Devices) result["devices"].push_back(DeviceJson(device));
    }
    if (page.Query.Topic == api::Topic::Programs) {
        result["totalRecords"] = page.TotalRecords;
        result["processes"] = Json::array();
        for (const auto &process : page.Processes) result["processes"].push_back(ProcessJson(process));
    }
    if (page.Query.Topic == api::Topic::FieldSources) {
        result["totalRecords"] = page.TotalRecords;
        result["fieldSources"] = Json::array();
        for (const auto &source : page.FieldSources) result["fieldSources"].push_back(FieldSourceJson(source));
    }
    if (page.Query.Topic == api::Topic::Runtime) {
        result["projectiles"] = Json::array();
        for (const auto &projectile : page.Projectiles) result["projectiles"].push_back(ProjectileJson(projectile));
    }
    if (page.Query.Topic == api::Topic::Knowledge) {
        result["totalRecords"] = page.TotalRecords;
        result["discoveries"] = Json::array();
        for (const auto &discovery : page.Discoveries)
            result["discoveries"].push_back(DiscoveryJson(discovery, page.View.Stamp.WorldId));
    }
    if (page.Query.Topic == api::Topic::History) {
        result["spatialScope"] = "global_archive";
        result["totalRecords"] = page.TotalRecords;
        result["firstSequence"] = std::to_string(*page.HistoryFirstSequence);
        result["highWatermark"] = std::to_string(*page.HistoryHighWatermark);
        result["history"] = Json::array();
        for (const auto &record : page.History) result["history"].push_back(HistoryRecordJson(record));
    }
    return result;
}

bool ExactFields(const Json &value, std::initializer_list<std::string_view> required,
                 std::initializer_list<std::string_view> optional = {}) {
    if (!value.is_object()) return false;
    for (const auto name : required) if (!value.contains(name)) return false;
    for (const auto &[name, unused] : value.items()) {
        if (std::find(required.begin(), required.end(), name) == required.end()
            && std::find(optional.begin(), optional.end(), name) == optional.end()) return false;
    }
    return true;
}

template<class Integer> bool Decimal(const Json &value, Integer &result) {
    if (!value.is_string()) return false;
    const auto &text = value.get_ref<const std::string &>();
    if (text.empty()) return false;
    size_t digit = 0;
    if constexpr (std::numeric_limits<Integer>::is_signed) {
        if (text.front() == '-') digit = 1;
    }
    if (digit == text.size() || text[digit] < '0' || text[digit] > '9') return false;
    if (text[digit] == '0' && (digit != 0 || text.size() != 1)) return false;
    for (size_t i = digit; i < text.size(); ++i) if (text[i] < '0' || text[i] > '9') return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

bool Unsigned32(const Json &value, uint32_t &result) {
    if (!value.is_number_integer()) return false;
    if (value.is_number_unsigned()) {
        const auto number = value.get<uint64_t>();
        if (number > UINT32_MAX) return false;
        result = uint32_t(number);
        return true;
    }
    const auto number = value.get<int64_t>();
    if (number < 0 || uint64_t(number) > UINT32_MAX) return false;
    result = uint32_t(number);
    return true;
}

bool ReadRect(const Json &value, api::Rect &scope) {
    return ExactFields(value, {"origin", "width", "height"})
        && ExactFields(value["origin"], {"x", "y"})
        && Decimal(value["origin"]["x"], scope.Origin.X) && Decimal(value["origin"]["y"], scope.Origin.Y)
        && Unsigned32(value["width"], scope.Width) && Unsigned32(value["height"], scope.Height);
}

bool ReadTopic(const Json &value, api::Topic &topic) {
    if (!value.is_string()) return false;
    for (uint32_t index = 0; index < uint32_t(api::Topic::Count); ++index) {
        const auto candidate = api::Topic(index);
        if (value == TopicName(candidate)) { topic = candidate; return true; }
    }
    return false;
}

// Bound parser recursion before constructing a DOM. Syntax and escaping are
// still validated by nlohmann; this scan only limits nesting outside strings.
bool BoundedDepth(std::string_view text) {
    uint32_t depth = 0;
    bool inString = false, escaped = false;
    for (const char character : text) {
        if (inString) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inString = false;
        } else if (character == '"') inString = true;
        else if (character == '{' || character == '[') { if (++depth > 16) return false; }
        else if (character == '}' || character == ']') { if (depth == 0) return false; --depth; }
    }
    return true;
}

Json ParseRequest(std::string_view request) {
    if (!BoundedDepth(request)) return Json(Json::value_t::discarded);
    bool duplicate = false;
    std::vector<std::unordered_set<std::string>> objectKeys;
    const auto callback = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start) objectKeys.emplace_back();
        else if (event == Json::parse_event_t::object_end) objectKeys.pop_back();
        else if (event == Json::parse_event_t::key)
            duplicate |= !objectKeys.back().insert(value.get<std::string>()).second;
        return true;
    };
    // allow_exceptions=false confines malformed JSON (including numeric overflow)
    // to a discarded value, without catching exceptions from domain code.
    auto parsed = Json::parse(request.begin(), request.end(), callback, false);
    return duplicate ? Json(Json::value_t::discarded) : parsed;
}

Json StatusJson(Status status) { return {{"status", StatusName(status)}}; }

Json DeliverJson(ViewService &service, ObserverId observer, PreparedResponse prepared) {
    if (!prepared.Delivery) return StatusJson(prepared.Result);
    return ToJson(service.Deliver(observer, prepared.Delivery));
}

Json HandleQuery(ViewService &service, ObserverId observer, const Json &request) {
    QuerySpec query;
    ViewId view = 0;
    CursorId cursor = 0;
    if (!ExactFields(request, {"method", "viewId", "topic", "scope"},
                     {"resolutionCells", "pageSize", "schemaVersion", "cursor"})
        || !Decimal(request["viewId"], view) || !ReadTopic(request["topic"], query.Topic)
        || !ReadRect(request["scope"], query.Scope)) return StatusJson(Status::InvalidArgument);
    if ((request.contains("resolutionCells") && !Unsigned32(request["resolutionCells"], query.ResolutionCells))
        || (request.contains("pageSize") && !Unsigned32(request["pageSize"], query.PageSize))
        || (request.contains("schemaVersion") && !Unsigned32(request["schemaVersion"], query.SchemaVersion))
        || (request.contains("cursor") && !Decimal(request["cursor"], cursor)))
        return StatusJson(Status::InvalidArgument);
    return DeliverJson(service, observer, service.Query(observer, view, query, cursor));
}

Json Dispatch(ViewService &service, ObserverId observer, const Json &request) {
    if (!request.is_object() || !request.contains("method") || !request["method"].is_string())
        return StatusJson(Status::InvalidArgument);
    const auto &method = request["method"].get_ref<const std::string &>();
    if (method == "DescribeCapabilities") {
        if (!ExactFields(request, {"method"})) return StatusJson(Status::InvalidArgument);
        return ToJson(service.DescribeCapabilities(observer));
    }
    if (method == "OpenView") {
        api::Rect scope;
        if (!ExactFields(request, {"method", "scope"}) || !ReadRect(request["scope"], scope))
            return StatusJson(Status::InvalidArgument);
        return ToJson(service.OpenView(observer, scope));
    }
    if (method == "Query") return HandleQuery(service, observer, request);
    if (method == "ReadFacts" || method == "ReadHistory") {
        ViewId view = 0;
        uint64_t after = 0;
        uint32_t limit = method == "ReadFacts" ? Limits::MaximumFacts : knowledge::HistoryCapacity;
        if (!ExactFields(request, {"method", "viewId", "after"}, {"limit"})
            || !Decimal(request["viewId"], view) || !Decimal(request["after"], after)
            || (request.contains("limit") && !Unsigned32(request["limit"], limit)))
            return StatusJson(Status::InvalidArgument);
        return DeliverJson(service, observer, method == "ReadFacts" ? service.ReadFacts(observer, view, after, limit)
            : service.ReadHistory(observer, view, after, limit));
    }
    if (method == "CloseView") {
        ViewId view = 0;
        if (!ExactFields(request, {"method", "viewId"}) || !Decimal(request["viewId"], view))
            return StatusJson(Status::InvalidArgument);
        return StatusJson(service.CloseView(observer, view));
    }
    return StatusJson(Status::InvalidArgument);
}
} // namespace

Json ToJson(const Response &response) {
    Json result = StatusJson(response.Result);
    // A malformed caller-created DTO must not turn a denial into a data leak.
    if (response.Result == Status::PermissionDenied || response.Result == Status::ViewRevoked
        || response.Result == Status::ViewExpired) return result;
    if (response.Page) result["page"] = PageJson(*response.Page);
    if (response.FactView) result["view"] = ViewJson(*response.FactView);
    if (response.Facts) result["facts"] = FactsJson(*response.Facts);
    if (response.History) result["history"] = HistoryJson(*response.History);
    return result;
}

Json ToJson(const OpenResult &result) {
    Json output = StatusJson(result.Result);
    if (result.Result == Status::Ok && result.View) output["view"] = ViewJson(*result.View);
    return output;
}

Json ToJson(const Capabilities &capabilities) {
    Json result = StatusJson(capabilities.Result);
    if (capabilities.Result != Status::Ok) return result;
    result["schema"] = "pixel.observation/1";
    result["schemaVersion"] = capabilities.SchemaVersion;
    result["facts"] = capabilities.Facts;
    if (capabilities.Region) result["region"] = RectJson(*capabilities.Region);
    result["topics"] = Json::array();
    for (const auto &capability : capabilities.Topics) {
        Json topic{{"topic", TopicName(capability.Topic)}, {"availability", AvailabilityName(capability.Support)}};
        if (capability.Support != api::Availability::Unsupported) {
            topic["resolutionCells"] = capability.Topic == api::Topic::AirVelocity ? Json::array({1, 16}) : Json::array({1});
            topic["unit"] = TopicUnit(capability.Topic);
            if (capability.Topic == api::Topic::Items || capability.Topic == api::Topic::Devices) {
                topic["inventoryAccess"] = {{"kind", capabilities.AllInventories ? "all"
                    : capabilities.InventoryOwner ? "owner" : "none"}};
                if (!capabilities.AllInventories && capabilities.InventoryOwner) {
                    topic["inventoryAccess"]["worldEpoch"] = std::to_string(capabilities.InventoryOwner->WorldEpoch);
                    topic["inventoryAccess"]["ownerActor"] = std::to_string(capabilities.InventoryOwner->OwnerActor);
                }
            }
            if (capability.Topic == api::Topic::Items) topic["pageUnit"] = "container";
            if (capability.Topic == api::Topic::Devices) topic["pageUnit"] = "device";
            if (capability.Topic == api::Topic::Programs) topic["pageUnit"] = "process";
            if (capability.Topic == api::Topic::FieldSources) topic["pageUnit"] = "field_source";
            if (capability.Topic == api::Topic::Knowledge) {
                topic["pageUnit"] = "discovery";
                topic["knowledgeAccess"] = {{"kind", capabilities.AllKnowledge ? "all"
                    : capabilities.KnowledgeOwner ? "owner" : "none"}};
                if (!capabilities.AllKnowledge && capabilities.KnowledgeOwner) {
                    topic["knowledgeAccess"]["worldEpoch"] = std::to_string(capabilities.KnowledgeOwner->WorldEpoch);
                    topic["knowledgeAccess"]["ownerActor"] = std::to_string(capabilities.KnowledgeOwner->OwnerActor);
                }
            }
            if (capability.Topic == api::Topic::History) {
                topic["pageUnit"] = "history_record";
                topic["spatialScope"] = "global_archive";
            }
            topic["source"] = SourceFor(capability.Topic);
        }
        result["topics"].push_back(std::move(topic));
    }
    result["limits"] = {{"maximumObservers", Limits::MaximumObservers}, {"maximumViewsPerObserver", 1},
                        {"maximumEdgeCells", Limits::MaximumEdge}, {"maximumPageCells", Limits::MaximumPageCells},
                        {"maximumPages", Limits::MaximumPages}, {"maximumCursorsPerView", Limits::MaximumCursors},
                        {"maximumPendingDeliveriesPerView", 1}, {"maximumFacts", Limits::MaximumFacts},
                        {"maximumHistoryRecords", knowledge::HistoryCapacity},
                        {"maximumViewBytes", std::to_string(Limits::MaximumViewBytes)},
                        {"leaseServiceFrames", std::to_string(Limits::LeaseFrames)},
                        {"maximumRequestBytes", JsonAdapter::MaximumRequestBytes},
                        {"maximumResponseBytes", JsonAdapter::MaximumResponseBytes}};
    return result;
}

Json ToJson(const DerivedMemory &memory) {
    Json result{{"provenance", DerivedMemory::Provenance}, {"id", std::to_string(memory.Id)},
                {"text", memory.Text}, {"sources", Json::array()}};
    for (const auto &source : memory.Sources)
        result["sources"].push_back({{"history", {{"worldId", source.History.WorldId},
                                                  {"sequence", std::to_string(source.History.Sequence)}}},
                                     {"sourceFactId", FactIdJson(source.SourceFact)},
                                     {"sourceVersion", StampJson(source.SourceVersion)}});
    return result;
}

Json ToJson(const api::ActionReceipt &receipt) {
    Json result{{"key", RequestKeyJson(receipt.Key)},
                {"status", receipt.Status == api::ActionStatus::Applied ? "Applied" : "Rejected"},
                {"reason", api::ReasonName(receipt.Rejection)}, {"resolvedAt", StampJson(receipt.ResolvedAt)},
                {"facts", {{"worldEpoch", std::to_string(receipt.Facts.WorldEpoch)},
                           {"firstSequence", std::to_string(receipt.Facts.FirstSequence)}, {"count", receipt.Facts.Count}}},
                {"message", receipt.Message}};
    std::visit(Overload{
        [&](const std::monostate &) {},
        [&](const api::DeviceChanged &value) { result["result"] = FactPayloadJson(value); },
        [&](const api::ForceObserved &value) { result["result"] = FactPayloadJson(value); },
        [&](const api::AuthorityChanged &value) { result["result"] = FactPayloadJson(value); },
        [&](const api::MaterialResult &value) {
            Json data{{"kind", "material"}, {"target", CellJson(value.Target)}, {"changed", value.Changed},
                      {"blocked", value.Blocked}, {"notReady", value.NotReady}};
            if (value.Before) data["before"] = *value.Before;
            if (value.After) data["after"] = *value.After;
            result["result"] = std::move(data);
        },
        [&](const api::ActivationResult &value) {
            result["result"] = {{"kind", "activation"}, {"worldEpoch", std::to_string(value.WorldEpoch)},
                                {"activationId", std::to_string(value.ActivationId)}, {"program", ProgramJson(value.Program)},
                                {"runningRoots", std::to_string(value.RunningRoots)},
                                {"projectiles", std::to_string(value.Projectiles)}, {"pending", std::to_string(value.Pending)}};
        },
        [&](const api::WindResult &value) {
            result["result"] = {{"kind", "wind"}, {"backendKind", WindName(value.Kind)},
                                {"boundaryY", std::to_string(value.BoundaryY)}};
        }
    }, receipt.Result);
    return result;
}

std::string JsonAdapter::Handle(ObserverId observer, std::string_view request) {
    if (request.size() > MaximumRequestBytes) return StatusJson(Status::LimitExceeded).dump();
    const auto parsed = ParseRequest(request);
    if (parsed.is_discarded()) return StatusJson(Status::InvalidArgument).dump();
    auto response = Dispatch(Service, observer, parsed).dump();
    if (response.size() > MaximumResponseBytes) return StatusJson(Status::LimitExceeded).dump();
    return response;
}
} // namespace pixel::llm
