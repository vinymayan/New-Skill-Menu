#pragma once

#include <nlohmann/json.hpp>

namespace RosterService
{
    std::vector<RE::Actor*> GetSelectableActors(const nlohmann::json& settings);
    bool IsActiveCompanion(RE::Actor* actor, const nlohmann::json& settings, bool isHighProcess);
}
