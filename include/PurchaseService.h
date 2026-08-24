#pragma once

#include <nlohmann/json.hpp>

namespace PurchaseService
{
    struct Result
    {
        bool success = false;
        std::string reason;
    };

    Result Purchase(
        RE::Actor* actor,
        RE::BGSPerk* perk,
        int perkPointCost,
        const nlohmann::json& customCosts,
        const nlohmann::json& resources);
}
