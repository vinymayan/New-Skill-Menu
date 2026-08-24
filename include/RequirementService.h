#pragma once

#include <nlohmann/json.hpp>

namespace RequirementService
{
    using json = nlohmann::json;

    struct ValidationResult
    {
        bool allowed = false;
        std::string reason;
    };

    ValidationResult ValidatePurchaseSnapshot(
        const json& snapshot,
        std::string_view perkId);
    json ValidateRules(const json& rules);
}
