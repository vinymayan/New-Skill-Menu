#include "RosterService.h"

#include "Manager.h"

namespace
{
    std::vector<std::string> Split(std::string_view value, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream stream{ std::string(value) };
        std::string item;
        while (std::getline(stream, item, delimiter)) result.push_back(item);
        return result;
    }

    RE::TESFaction* ResolveFaction(std::string_view value)
    {
        if (auto form = RE::TESForm::LookupByEditorID(value)) {
            if (auto faction = form->As<RE::TESFaction>()) return faction;
        }
        auto tokens = Split(value, '|');
        if (tokens.size() != 2) return nullptr;
        try {
            const auto localID = static_cast<RE::FormID>(
                std::stoul(tokens[1], nullptr, 16));
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ?
                dataHandler->LookupForm<RE::TESFaction>(localID, tokens[0]) :
                nullptr;
        }
        catch (...) {
            return nullptr;
        }
    }

    RE::TESFaction* ResolveFaction(const nlohmann::json& value)
    {
        if (value.is_string()) {
            return ResolveFaction(std::string_view(value.get_ref<const std::string&>()));
        }
        if (!value.is_object()) return nullptr;
        const auto editorID = value.find("editorID");
        if (editorID != value.end() && editorID->is_string() && !editorID->empty()) {
            if (auto form = RE::TESForm::LookupByEditorID(editorID->get<std::string>())) {
                if (auto faction = form->As<RE::TESFaction>()) return faction;
            }
        }
        const auto fallback = value.find("form");
        return fallback != value.end() && fallback->is_string() ?
            ResolveFaction(std::string_view(fallback->get_ref<const std::string&>())) : nullptr;
    }

    std::vector<RE::TESFaction*> ResolveFactions(
        const nlohmann::json& settings,
        std::string_view field,
        std::string_view vanillaFallback)
    {
        std::vector<RE::TESFaction*> factions;
        auto detection = settings.value(
            "followerDetection",
            nlohmann::json::object());
        auto configured = detection.value(
            std::string(field),
            nlohmann::json::array());
        if (configured.is_array()) {
            for (const auto& value : configured) {
                if (auto faction = ResolveFaction(value)) {
                    factions.push_back(faction);
                }
            }
        }
        if (!vanillaFallback.empty()) {
            if (auto vanilla = ResolveFaction(vanillaFallback);
                vanilla &&
                std::ranges::find(factions, vanilla) == factions.end()) {
                factions.push_back(vanilla);
            }
        }
        return factions;
    }

    bool InAnyFaction(
        RE::Actor* actor,
        const std::vector<RE::TESFaction*>& factions)
    {
        return actor && std::ranges::any_of(
            factions,
            [&](auto faction) {
                return faction && actor->IsInFaction(faction);
            });
    }

    bool IsLydia(RE::Actor* actor)
    {
        if (!actor) return false;
        auto actorBase = actor->GetActorBase();
        const auto name = actor->GetName();
        return actor->GetFormID() == 0x000A2C94 ||
            (actorBase && actorBase->GetFormID() == 0x000A2C8E) ||
            (name && _stricmp(name, "Lydia") == 0);
    }

    void LogDiagnostics(
        RE::Actor* actor,
        bool isHighProcess,
        bool currentFollower,
        bool potentialFollower,
        bool accepted,
        std::string_view reason)
    {
        if (!actor) return;
        if (!IsLydia(actor) &&
            !actor->IsPlayerTeammate() &&
            !currentFollower &&
            !potentialFollower) {
            return;
        }
        const auto lifeState = actor->GetLifeState();
        const auto diagnostic = fmt::format(
            "{}|{}|{}|{}|{}|{}|{}",
            isHighProcess,
            static_cast<std::uint32_t>(lifeState),
            actor->IsPlayerTeammate(),
            currentFollower,
            potentialFollower,
            accepted,
            reason);
        static std::unordered_map<RE::FormID, std::string> previous;
        if (previous[actor->GetFormID()] == diagnostic) return;
        previous[actor->GetFormID()] = diagnostic;

        auto commander = actor->GetCommandingActor();
        logger::info(
            "[CompanionFilter]{} name='{}' actor={:08X} highProcess={} "
            "lifeState={} teammate={} currentFollower={} potentialFollower={} "
            "commanded={} commander={:08X} summoned={} accepted={} reason={}",
            IsLydia(actor) ? "[LYDIA]" : "",
            actor->GetName(),
            actor->GetFormID(),
            isHighProcess,
            static_cast<std::uint32_t>(lifeState),
            actor->IsPlayerTeammate(),
            currentFollower,
            potentialFollower,
            actor->IsCommandedActor(),
            commander ? commander->GetFormID() : 0,
            actor->IsSummoned(),
            accepted,
            reason);
    }
}

bool RosterService::IsActiveCompanion(
    RE::Actor* actor,
    const nlohmann::json& settings,
    bool isHighProcess)
{
    if (!actor) return false;

    if (!settings.value("followerDetection", nlohmann::json::object()).value("enabled", true)) return false;

    const auto currentFactions = ResolveFactions(
        settings,
        "currentFollowerFactions",
        "Skyrim.esm|1CA7D");
    const auto potentialFactions = ResolveFactions(
        settings,
        "potentialFollowerFactions",
        "Skyrim.esm|5C84D");
    const auto excludedFactions = ResolveFactions(
        settings,
        "excludedFollowerFactions",
        "");
    const bool currentFollower = InAnyFaction(actor, currentFactions);
    const bool potentialFollower = InAnyFaction(actor, potentialFactions);
    const bool excluded = InAnyFaction(actor, excludedFactions);

    auto detection = settings.value(
        "followerDetection",
        nlohmann::json::object());
    const bool allowPlayerTeammates =
        detection.value("allowPlayerTeammates", true);
    const bool allowSummoned =
        detection.value("allowSummoned", false);

    const auto lifeState = actor->GetLifeState();
    const bool dead =
        lifeState == RE::ACTOR_LIFE_STATE::kDying ||
        lifeState == RE::ACTOR_LIFE_STATE::kDead;

    bool accepted = false;
    std::string_view reason = "not_player_teammate";
    if (!isHighProcess) reason = "not_in_high_process";
    else if (dead) reason = "dead_life_state";
    else if (actor->IsDisabled()) reason = "disabled";
    else if (excluded) reason = "excluded_faction";
    else if (actor->IsSummoned() && !allowSummoned) reason = "summoned";
    else if (currentFollower) {
        accepted = true;
        reason = "current_follower_faction";
    }
    else if (actor->IsPlayerTeammate() && potentialFollower) {
        accepted = true;
        reason = "teammate_and_potential_follower";
    }
    else if (actor->IsPlayerTeammate() && allowPlayerTeammates) {
        accepted = true;
        reason = "player_teammate";
    }
    else if (actor->IsPlayerTeammate()) {
        reason = "teammate_not_allowed";
    }

    LogDiagnostics(
        actor,
        isHighProcess,
        currentFollower,
        potentialFollower,
        accepted,
        reason);
    return accepted;
}

std::vector<RE::Actor*> RosterService::GetSelectableActors(
    const nlohmann::json& settings)
{
    std::vector<RE::Actor*> actors;
    std::unordered_set<RE::FormID> seen;
    auto player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        actors.push_back(player);
        seen.insert(player->GetFormID());
        Manager::GetSingleton()->RehydratePurchasedPerks(player);
    }

    auto processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return actors;

    for (auto& handle : processLists->highActorHandles) {
        auto pointer = handle.get();
        auto actor = pointer.get();
        if (!actor || actor->IsPlayerRef()) continue;
        if (IsActiveCompanion(actor, settings, true) &&
            seen.insert(actor->GetFormID()).second) {
            Manager::GetSingleton()->RehydratePurchasedPerks(actor);
            actors.push_back(actor);
        }
    }
    return actors;
}
