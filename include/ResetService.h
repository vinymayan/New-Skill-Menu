#pragma once

#include <nlohmann/json.hpp>
#include <unordered_set>

namespace ResetService
{
    using json = nlohmann::json;

    std::unordered_set<RE::FormID> PerksInTree(const json& tree);
    json Preview(
        RE::Actor* actor,
        const std::unordered_set<RE::FormID>& allowedPerks,
        int maxResets);
    json Execute(
        RE::Actor* actor,
        const std::unordered_set<RE::FormID>& allowedPerks,
        const json& resources,
        int maxPerkPoints,
        int maxResets,
        bool countReset);
}
