#pragma once

#include <nlohmann/json.hpp>

namespace SnapshotService
{
    struct PerkOwnership
    {
        bool owned = false;
        bool runtime = false;
        bool actorBase = false;
        bool templateBase = false;
        bool purchased = false;
        std::string source = "none";
    };

    PerkOwnership GetPerkOwnership(RE::Actor* actor, RE::BGSPerk* perk);
    nlohmann::json BuildActorSummary(RE::Actor* actor);
}
