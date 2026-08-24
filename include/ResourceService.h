#pragma once

#include "EconomyTypes.h"
#include <nlohmann/json.hpp>

namespace ResourceService
{
    using json = nlohmann::json;

    void SetDefinitions(const json& definitions);
    json GetDefinitions();
    json BuildValues(RE::Actor* actor, const json& definitions);
    float GetValue(RE::Actor* actor, std::string_view resourceId, const json& definitions);
    bool Debit(
        RE::Actor* actor,
        const json& costs,
        const json& definitions,
        std::vector<PaidResource>& paid,
        std::string& error);
    void Refund(RE::Actor* actor, const std::vector<PaidResource>& paid);
    bool Credit(
        RE::Actor* actor,
        std::string_view resourceId,
        float amount,
        const json& definitions,
        std::string& error);
    bool Modify(
        RE::Actor* actor,
        std::string_view resourceId,
        float amount,
        const json& definitions,
        std::string& error);
}
